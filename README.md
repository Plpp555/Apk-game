# TriSync 3D Benchmark

Android・Windows・Windows 10 Mobile で **計算式と描画方式を完全に一致させた** 3D ベンチマークです。
全プラットフォームが同じ C++ コア (`core/`) と同じ GLSL ES 3.00 シェーダーを実行し、
CPU (マルチスレッドのジョブシステム) と GPU が並行して仕事を分担することで、互いの待ちを最小化します。

> 現在の配布物: **Android APK** (`dist/TriSync3D-1.0.0.apk`)。Windows / Windows 10 Mobile 版は同じコアを使って実装予定です
> (移植手順は [docs/SPEC.md](docs/SPEC.md) §8)。

## 特長

- **計算式の一致を証明**: OS の数学ライブラリを使わない決定論的な計算 (`core/detmath.h`)。各テストのシミュレーション状態を
  64bit ハッシュ化し、全プラットフォーム共通の参照値と照合して結果画面に「一致 ✓」と表示します。
  x86_64 と ARM64 (gcc / clang) でビット単位の一致を確認済み。
- **描画方式の同期**: 1920×1080 固定オフスクリーン、同一シェーダー、同一ポストプロセス (HDR → ブルーム → ACES)。
  Windows / W10M は ANGLE (OpenGL ES 3.0 → Direct3D 11) で同じ描画パスを実行します。
- **CPU と GPU の分担 (詰まりのない設計)**: ワーカースレッドがフレーム N+1 を計算してマップ済み GPU バッファに直接書き込む間、
  GPU はフレーム N を描画。トリプルバッファのリングをフェンスで保護し、ドライバの暗黙同期によるストールを避けます。
- **ボトルネック分析**: テスト毎に CPU ジョブ時間・GPU 時間・待ち時間を計測し、CPU律速 / GPU律速 / 均衡 を表示。

## テスト

| テスト | 内容 |
|---|---|
| GT1 小惑星帯 | 32,768 個の小惑星の軌道計算・カリング・LOD (CPU) + インスタンス描画・バンプマッピング (GPU) |
| GT2 星雲の嵐 | 131,072 粒子の物理計算と毎フレーム 2.6 MB のストリーミング (CPU) + 加算合成フィルレート (GPU) |
| GT3 ネオンシティ | 256 光源のクラスタ割り当て (CPU, Forward+) + GGX ライティング (GPU) |
| CPU 物理演算 | 24,576 球の衝突シミュレーション。シングル/マルチスレッドで実行し、結果の一致も検証 |

スコア計算式:

```
グラフィックス = 100 × GT1〜GT3 の FPS の調和平均
CPU            = マルチスレッドの球ステップ毎秒 / 2,500
総合           = 1 / (0.75 / グラフィックス + 0.25 / CPU)
```

詳細は [docs/SPEC.md](docs/SPEC.md) を参照してください。

## インストール (Android)

1. `dist/TriSync3D-1.0.0.apk` を端末にコピーして開きます (「提供元不明のアプリ」の許可が必要)。
2. 要件: Android 7.0 以上、OpenGL ES 3.0 対応 GPU。対応 ABI: arm64-v8a / x86_64
   (Gradle + NDK でビルドした場合は armeabi-v7a も含みます)。
3. 「ベンチマーク開始」で標準計測 (公式スコア、約 2〜5 分)、「クイック実行」で短縮計測 (非公式)。

## ビルド

### A. Android Studio / Gradle (公式 SDK + NDK)

```bash
cd android
./gradlew assembleRelease      # → android/app/build/outputs/apk/release/app-release.apk
```

NDK r27 と CMake 3.22.1 を使用します (`android/app/build.gradle`)。GitHub Actions でも同じビルドを実行し、APK を成果物として保存します。

### B. SDK / NDK なしのオフラインビルド

Android SDK のダウンロードができない環境向けに、Ubuntu のパッケージだけで APK を作れます。

```bash
sudo apt-get install clang lld llvm cmake ninja-build openjdk-17-jdk-headless \
  aapt dalvik-exchange zipalign apksigner android-sdk-platform-23 \
  libegl-dev libgles-dev android-libnativehelper-dev
tools/build_apk_offline.sh     # → build/offline/TriSync3D-1.0.0.apk
```

ホストの clang で `tools/android-sysroot` の最小 bionic ヘッダーに対してコンパイルし、Android システムライブラリの
スタブにリンクします。`tools/check_android_elf.py` が依存ライブラリ・未定義シンボル・16 KB ページ整列を検査します。

### C. 開発用ツール (Linux)

```bash
cmake -S . -B build/linux -G Ninja && cmake --build build/linux
build/linux/simcheck --threads 3          # GPU なしでシミュレーションの参照ハッシュを出力
EGL_PLATFORM=surfaceless build/linux/bench_host --frames 30 --dump /tmp   # ヘッドレスで全テストを実行
```

シミュレーションを変更した場合は `build/linux/simcheck --header > core/reference_hashes.h` で参照値を更新します。

### D. 自動実行 (adb / CI)

```bash
adb install dist/TriSync3D-1.0.0.apk
adb shell am start -n com.plpp555.trisync3d/.MainActivity --es autostart quick   # standard / quick
adb pull /sdcard/Android/data/com.plpp555.trisync3d/files/result.json            # 結果 JSON
```

`--ei frames N` を付けるとフレーム数を短縮したスモークテストになります (非公式扱い)。結果は logcat (タグ `TriSync3D`、`RESULT ` 行) にも出力されます。
GitHub Actions (`.github/workflows/android.yml`) は、決定性チェック (x86_64 / ARM64)、Gradle + NDK ビルド、オフラインビルドに加えて、
Android エミュレータ (API 30 / 34、x86_64 と ARM 変換経由の arm64-v8a) で APK を実際に起動し、クイック計測と参照ハッシュの一致を検証します
(`tools/ci/emulator_run.sh`, `tools/ci/check_result.py`)。

## ディレクトリ構成

```
core/               共通 C++ コア (決定論的数学, ジョブシステム, シミュレーション, GLES3 描画, 計測/スコア)
platform/posix/     pthread / 時刻 (Android, Linux)
platform/android/   JNI + EGL 描画スレッド
hosts/linux/        ヘッドレス EGL ホスト (Windows 版ホストの雛形)
hosts/simcheck/     決定性検証ツール
android/            Android アプリ (Gradle プロジェクト, Java UI)
tools/              オフラインビルド, 最小 sysroot, 署名鍵 (公開デバッグ鍵)
docs/SPEC.md        ベンチマーク仕様 (計算式・パイプライン・スコア・移植ガイド)
dist/               ビルド済み APK
```

## 署名について

APK は `tools/keystore/trisync-debug.jks` (**公開デバッグ鍵**) で署名しています。一般配布する場合は独自の鍵で署名してください。
