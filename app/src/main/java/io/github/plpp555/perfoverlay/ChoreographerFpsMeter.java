package io.github.plpp555.perfoverlay;

import android.os.SystemClock;
import android.view.Choreographer;

/**
 * 簡易モード: 自プロセスに届く VSYNC の回数を数える。
 * 非 root では他アプリの描画回数は取れないため、これは「画面のリフレッシュレート（Hz）」に相当する。
 */
final class ChoreographerFpsMeter implements FpsMeter, Choreographer.FrameCallback {
    private final Choreographer choreographer = Choreographer.getInstance();
    private boolean active;
    private int frames;
    private long lastSampleNanos = SystemClock.elapsedRealtimeNanos();

    @Override
    public String label() {
        return "Hz";
    }

    @Override
    public void setActive(boolean active) {
        if (this.active == active) {
            return;
        }
        this.active = active;
        if (active) {
            frames = 0;
            lastSampleNanos = SystemClock.elapsedRealtimeNanos();
            choreographer.postFrameCallback(this);
        } else {
            choreographer.removeFrameCallback(this);
        }
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        if (!active) {
            return;
        }
        frames++;
        choreographer.postFrameCallback(this);
    }

    @Override
    public float sample() {
        long now = SystemClock.elapsedRealtimeNanos();
        long elapsed = now - lastSampleNanos;
        float fps = elapsed > 0 ? frames * 1e9f / elapsed : 0f;
        frames = 0;
        lastSampleNanos = now;
        return fps;
    }

    @Override
    public void release() {
        setActive(false);
    }
}
