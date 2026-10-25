# TriSync 3D ベンチマーク仕様書 (v1.0.0)

Android / Windows / Windows 10 Mobile で **同一の計算式・同一の描画方式** によってスコアを算出するための仕様です。
この文書に書かれた定数はすべて `core/bench_spec.h` と各 `core/sim_*.cpp` に実装されており、
変更するとスコアと参照ハッシュ (`core/reference_hashes.h`) が変わります。

---

## 1. 基本原則

| 原則 | 実装 |
|---|---|
| 全プラットフォームで同じコード | `core/` の C++17 コードを全OSで共有。OS依存部分は `platform/` と `hosts/` のみ |
| 同じ計算式 | 決定論的数学ライブラリ `core/detmath.h`。OSの `libm` (sinf/cosf/expf) は一切使わない |
| 同じ描画方式 | OpenGL ES 3.0 + GLSL ES 3.00 の同一シェーダー。Windows / W10M は ANGLE (GLES→Direct3D 11) 経由 |
| 同じ負荷 | 解像度 1920×1080 固定のオフスクリーン描画、1フレーム = 1/60 秒の固定タイムステップ、固定フレーム数 |
| 計算一致の証明 | シミュレーション状態の 64bit ハッシュを全プラットフォーム共通の参照値と照合 |
| CPU と GPU の分担 | ワーカースレッドがフレーム N+1 を計算中に GPU がフレーム N を描画（§5） |

## 2. アーキテクチャ

```
core/                     ← 全プラットフォーム共通 (STL・libm 非依存)
  detmath.h  rng.h        決定論的数学・乱数・ハッシュ
  jobs.*                  ジョブシステム (固定チャンク分割)
  sim_*.cpp               各テストのシミュレーション (GL 非依存)
  gl_util.* render_common.* shaders.cpp test_*.cpp   GLES 3.0 描画
  benchmark.*             計測・スコア計算・JSON 出力
platform/posix/           スレッド・時刻 (Android / Linux)
platform/android/         JNI + EGL 描画スレッド
hosts/linux/              ヘッドレス EGL ホスト (開発・CI・Windows 版の雛形)
hosts/simcheck/           GPU なしの決定性検証ツール
android/                  Android アプリ (Java UI)
```

| プラットフォーム | グラフィックス API | ウィンドウ / コンテキスト | スレッド |
|---|---|---|---|
| Android 7.0+ (arm64-v8a / armeabi-v7a / x86_64) | OpenGL ES 3.0 (ネイティブ) | EGL + ANativeWindow | pthread |
| Windows 10/11 (x64 / ARM64) | OpenGL ES 3.0 via ANGLE → Direct3D 11 | EGL (ANGLE) + HWND | Win32 (予定: `platform/windows`) |
| Windows 10 Mobile (ARM, UWP) | OpenGL ES 3.0 via ANGLE → Direct3D 11 (FL 10_0 以上) | EGL (ANGLE) + CoreWindow | Win32 / UWP |

## 3. 決定論的計算 (計算式の一致)

浮動小数点の結果をビット単位で一致させるため、以下を **規約** とします。

1. **使用する演算は IEEE-754 で正しく丸められるもののみ**: `+ - * /`、`sqrt`、float⇔int32 変換。
2. **超越関数は自前の多項式** (`core/detmath.h`)
   - `sin(x)`: Cody–Waite 法で `[-π, π]` に縮約 (`2π = 6.28125 + 1.9353071795864769e-3`)、`[-π/2, π/2]` に折り返し、11 次奇多項式
   - `cos(x) = sin(x + π/2)`
   - `exp2(x)`: 整数部は指数ビット、小数部は 6 次多項式
   - `floor(x)`: int32 への切り捨て変換で実装 (丸めモード非依存)
3. **FMA 融合の禁止**: clang/gcc は `-ffp-contract=off -fno-fast-math`、MSVC は `/fp:precise`
   (VS2022 以降は既定で縮約なし)。ARM64 の clang は既定で `a*b+c` を FMA 命令に融合するため必須。
4. **評価順序の固定**: 1 つの式・引数リストに乱数呼び出しを 2 つ以上書かない。
   C++ は関数引数の評価順が未規定で、gcc は右→左、clang は左→右に評価するため結果が変わる
   (実際に開発中に検出: `core/rng.h` の `rand_v3()` を使用)。
5. **乱数**: PCG32 (XSH-RR)、`unit() = (next() >> 8) / 2^24` (正確に表現可能)。
6. **並列化の決定性**: 処理はスレッド数に依存しない固定数のチャンクに分割。アトミックな集計は使わず、
   チャンク毎のカウント → 直列のプレフィックス和 → チャンク毎の書き込み、で順序を固定。
   物理演算は Jacobi 型 (ステップ n を読み n+1 に書く) で、スレッド間の書き込み競合がない。
7. **検証ハッシュ**: FNV-1a 64bit で状態配列の float ビット列をハッシュ化。

### 検証結果 (開発環境で確認済み)

| ビルド | GT1 | GT2 | GT3 | CPU |
|---|---|---|---|---|
| x86_64 gcc 13 / clang 18 | 一致 | 一致 | 一致 | 一致 |
| ARM64 gcc 13 / clang 18 (qemu) | 一致 | 一致 | 一致 | 一致 |
| スレッド数 0 / 1 / 3 | 一致 | 一致 | 一致 | 一致 |
| Android 14 エミュレータ x86_64 (APK 内で計測) | 一致 | 一致 | 一致 | 一致 |
| Android 14 エミュレータ arm64-v8a (ARM 変換, APK 内で計測) | 一致 | 一致 | 一致 | 一致 |
| Android 11 エミュレータ x86_64 (APK 内で計測) | 一致 | 一致 | 一致 | 一致 |

CI (`.github/workflows/android.yml`) が x86_64 と ARM64 の両方で毎回 `core/reference_hashes.h` と照合します。

## 4. テスト内容

すべてのグラフィックステストは **1920×1080 RGBA16F** のシーンターゲットに描画し、
ブルーム (1/2・1/4・1/8 解像度、分離ガウス) と ACES トーンマップで RGBA8 に変換します。
(`EXT_color_buffer_half_float` / `EXT_color_buffer_float` がない端末のみ RGB10_A2 にフォールバックし、結果に記録)

| テスト | CPU の仕事 (ワーカースレッド) | GPU の仕事 |
|---|---|---|
| **GT1 Asteroid Belt** | 32,768 個の小惑星の重力積分 (恒星 + 摂動する月)、クォータニオン回転積分、視錐台カリング、距離 LOD 選択、メッシュ×LOD 12 バケットへの詰め込み | インスタンス描画 (1280/320/80 三角形の 3 LOD)、頂点シェーダーでのクォータニオン回転、ノイズ + 微分バンプマッピング、恒星・星雲スカイ |
| **GT2 Nebula Storm** | 131,072 粒子: 4 つの移動アトラクター重力 + 渦場 + 波動場 (粒子毎に sin/cos)、寿命と再生成、色計算。毎フレーム約 2.6 MB を GPU バッファへ直接書き込み | 加算合成ビルボード 131,072 枚 (フィルレート負荷)、星雲スカイ (fbm) |
| **GT3 Neon City** | 256 光源 (車) の街路移動、ビュー空間 AABB による 16×8×16 クラスタへの光源割り当て (Forward+)、6,400 ビルのカリングと前後ソート | クラスタ毎の GGX + Lambert ライティング、窓・路面の手続き的マテリアル、霧 |
| **CPU Physics** | 24,576 球の離散要素法: 一様グリッド (並列カウンティングソート) + 接触ばね・ダンパ + 壁 + 移動障害物。シングルスレッド 150 ステップ、マルチスレッド 600 ステップ | (プレビューのみ。計測時間から除外) |

フレーム数: 標準 = 各 GT 900 フレーム (+30 ウォームアップ)、クイック = 300 フレーム (+15)。
カメラ・アトラクター・光源などはすべて **フレーム番号の関数** で、端末の速度に関係なく全端末で同じフレーム列を描画します。

## 5. CPU と GPU の分担 (パイプライン)

```
描画スレッド    | end_sim(N) | kick(N+1) | render(N) 送信 | fence(N) | end_sim(N+1) | kick(N+2) | ...
ワーカー群      |            | ===== シミュレーション N+1 (マップ済みバッファへ直接書込) ===== |
GPU             | ... フレーム N-1 実行 ... | ......... フレーム N 実行 ......... |
```

- **トリプルバッファのストリーミングリング** (`gl::StreamRing`): スロット `N mod 3` を
  `GL_MAP_UNSYNCHRONIZED_BIT` でマップし、ワーカーが直接書き込む (コピーなし)。
  そのスロットを最後に使ったフレーム (N-2) の `glFenceSync` を待ってから再利用するため、
  ドライバの暗黙同期によるストールが起きない。
- **ジョブチェーン**: 1 フレームの CPU 処理 (例: 積分 → プレフィックス和 → 詰め込み) は
  フェーズの連鎖としてワーカーに丸ごと渡し、フェーズ間の受け渡しもワーカー同士で行う。
  描画スレッドは GL コマンド発行に専念し、待つ必要がある時だけチャンクを手伝う。
- **GT3 の非同期アップロード**: 光源 UBO・クラスタテクスチャ (PBO 経由)・インスタンスも同じリングで転送。
- マップできないドライバでは自動的に `glBufferSubData` によるステージングへ切り替える。

結果画面には 1 フレームあたりの「CPU ジョブ時間」「GPU 時間 (EXT_disjoint_timer_query)」
「シミュレーション待ち」「フェンス待ち」から推定したボトルネック (CPU律速 / GPU律速 / 均衡) を表示します。

## 6. 計測方法

1. テストの準備 (メッシュ生成・シェーダーコンパイル) は計測外。
2. ウォームアップ後に `glFinish()` → シミュレーションを初期状態に戻して計測開始。
3. 計測区間 = 最初の `kick(1)` から最終フレーム送信後の `glFinish()` 完了まで (GPU 処理をすべて含む)。
4. 画面へのプレビュー表示は **最大 20 Hz** (50 ms 毎) に間引き、垂直同期の影響を受けないようにする。
5. `FPS = フレーム数 / 計測時間`、`1% Low = 99 パーセンタイルのフレーム時間の逆数`。
6. CPU テストは各ステップの実行時間のみを合計 (プレビュー描画とハッシュ計算は除外)。

## 7. スコア計算式

```
GraphicsScore = 100 × 3 / (1/FPS_GT1 + 1/FPS_GT2 + 1/FPS_GT3)        (FPS の調和平均 × 100)
CPUScore      = (24,576 × 600) / マルチスレッド計測秒 / 2,500          (球ステップ毎秒 / 2,500)
CPUSingle     = (24,576 × 150) / シングルスレッド計測秒 / 2,500
Overall       = 1 / (0.75 / GraphicsScore + 0.25 / CPUScore)          (重み付き調和平均)
```

**公式スコア** の条件: 標準モード、全テスト実行、全ハッシュが参照値と一致、シングル/マルチの結果が一致。

## 8. Windows / Windows 10 Mobile への移植ガイド

`core/` はそのまま MSVC でコンパイルできるように書かれています (STL・例外・RTTI・libm 不使用、
`atomics.h` は MSVC の `_Interlocked*` に対応済み)。必要な作業:

1. `platform/windows/platform_win32.cpp`: `core/platform.h` の実装
   (`CreateThread`、`SRWLOCK`、`CONDITION_VARIABLE`、`QueryPerformanceCounter`、`_aligned_malloc`)。
2. `hosts/windows/`: `hosts/linux/host_main.cpp` と同じ構成で、ANGLE の
   `eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, … D3D11 …)` + HWND (デスクトップ) /
   CoreWindow (UWP / Windows 10 Mobile) を使い、`Benchmark::tick()` を回すだけ。
3. コンパイラ設定: `/fp:precise` (`CMakeLists.txt` に設定済み)。`simcheck` を Windows でビルドし、
   出力が `core/reference_hashes.h` と一致することを確認する。
4. Windows 10 Mobile: ANGLE の GLES 3.0 には Direct3D 11 Feature Level 10_0 以上の GPU が必要。
