package io.github.plpp555.perfoverlay;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.view.Display;

/**
 * キャプチャなしモード: OS が持つ画面のリフレッシュレート（Hz）を 1 秒に 1 回読むだけ。
 * 非 root では他アプリの描画回数は取れないため FPS の代わりに表示する。
 * VSYNC ごとの処理をしないので、古い端末でも負荷はほぼゼロ。
 */
final class RefreshRateMeter implements FpsMeter {
    private final Display display;

    RefreshRateMeter(Context context) {
        DisplayManager dm = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
        display = dm.getDisplay(Display.DEFAULT_DISPLAY);
    }

    @Override
    public String label() {
        return "Hz";
    }

    @Override
    public void setActive(boolean active) {
    }

    @Override
    public float sample() {
        return display != null ? display.getRefreshRate() : 0f;
    }

    @Override
    public void release() {
    }
}
