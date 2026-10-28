package io.github.plpp555.perfoverlay;

import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * 高精度モード: 画面を縮小ミラーした仮想ディスプレイを作り、届いたフレーム数を数える。
 * SurfaceFlinger は画面の内容が変化したときだけ仮想ディスプレイへフレームを送るので、
 * フレーム数 = 実際に画面が更新された回数 = 前面のゲームの FPS になる。
 * 縮小（1/8）して受け取ったら即破棄するので負荷は小さい。
 */
final class CaptureFpsMeter implements FpsMeter {
    private static final int SCALE_DOWN = 8;

    private final AtomicInteger frames = new AtomicInteger();
    private MediaProjection projection;
    private MediaProjection.Callback callback;
    private HandlerThread thread;
    private ImageReader reader;
    private VirtualDisplay display;
    private long lastSampleNanos = SystemClock.elapsedRealtimeNanos();
    private boolean released;

    /**
     * @param onStopped ユーザーやシステムが画面キャプチャを止めたときにメインスレッドで呼ばれる
     * @throws RuntimeException 画面キャプチャを開始できなかった場合
     */
    @SuppressWarnings("deprecation")
    CaptureFpsMeter(Context context, int resultCode, Intent data, final Runnable onStopped) {
        try {
            MediaProjectionManager mpm =
                    (MediaProjectionManager) context.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
            projection = mpm.getMediaProjection(resultCode, data);
            if (projection == null) {
                throw new IllegalStateException("MediaProjection is null");
            }
            // Android 14 以降は createVirtualDisplay より前にコールバック登録が必須
            callback = new MediaProjection.Callback() {
                @Override
                public void onStop() {
                    if (!released) {
                        release();
                        onStopped.run();
                    }
                }
            };
            projection.registerCallback(callback, new Handler(Looper.getMainLooper()));

            DisplayMetrics dm = new DisplayMetrics();
            WindowManager wm = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
            wm.getDefaultDisplay().getRealMetrics(dm);
            int width = Math.max(2, (dm.widthPixels / SCALE_DOWN) & ~1);
            int height = Math.max(2, (dm.heightPixels / SCALE_DOWN) & ~1);

            thread = new HandlerThread("fps-capture");
            thread.start();
            Handler captureHandler = new Handler(thread.getLooper());
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 3);
            reader.setOnImageAvailableListener(new ImageReader.OnImageAvailableListener() {
                @Override
                public void onImageAvailable(ImageReader r) {
                    try {
                        Image image;
                        while ((image = r.acquireNextImage()) != null) {
                            frames.incrementAndGet();
                            image.close();
                        }
                    } catch (IllegalStateException ignored) {
                        // 解放処理と競合したときは無視
                    }
                }
            }, captureHandler);

            display = projection.createVirtualDisplay("PerfOverlay-fps", width, height,
                    Math.max(1, dm.densityDpi / SCALE_DOWN),
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                    reader.getSurface(), null, null);
            if (display == null) {
                throw new IllegalStateException("createVirtualDisplay failed");
            }
        } catch (RuntimeException e) {
            release();
            throw e;
        }
    }

    @Override
    public String label() {
        return "FPS";
    }

    @Override
    public void setActive(boolean active) {
        if (active) {
            // 画面 OFF の間に溜まった値を捨てる
            sample();
        }
    }

    @Override
    public float sample() {
        long now = SystemClock.elapsedRealtimeNanos();
        long elapsed = now - lastSampleNanos;
        int count = frames.getAndSet(0);
        lastSampleNanos = now;
        return elapsed > 0 ? count * 1e9f / elapsed : 0f;
    }

    @Override
    public void release() {
        if (released) {
            return;
        }
        released = true;
        if (display != null) {
            display.release();
        }
        if (projection != null) {
            try {
                if (callback != null) {
                    projection.unregisterCallback(callback);
                }
                projection.stop();
            } catch (RuntimeException ignored) {
                // 既に停止済み
            }
        }
        final ImageReader r = reader;
        final HandlerThread t = thread;
        if (t != null) {
            // リスナーと同じスレッドで閉じて競合を避ける
            new Handler(t.getLooper()).post(new Runnable() {
                @Override
                public void run() {
                    if (r != null) {
                        r.close();
                    }
                    t.quit();
                }
            });
        } else if (r != null) {
            r.close();
        }
    }
}
