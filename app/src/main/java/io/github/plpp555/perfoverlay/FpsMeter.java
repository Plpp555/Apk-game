package io.github.plpp555.perfoverlay;

/** FPS の計測方式。メインスレッドから呼び出す。 */
interface FpsMeter {
    /** オーバーレイに表示するラベル（"FPS" / "Hz"）。 */
    String label();

    /** 画面 OFF の間は false にして無駄な処理を止める。 */
    void setActive(boolean active);

    /** 前回呼び出しからの平均フレームレート。 */
    float sample();

    void release();
}
