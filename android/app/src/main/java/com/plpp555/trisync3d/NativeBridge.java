package com.plpp555.trisync3d;

import android.view.Surface;

/** JNI entry points of libtrisync3d.so (platform/android/jni_main.cpp). */
final class NativeBridge {
    static {
        System.loadLibrary("trisync3d");
    }

    static final int MODE_STANDARD = 0;
    static final int MODE_QUICK = 1;

    private NativeBridge() {}

    /** Starts the native render thread (idempotent). */
    static native void nativeInit(int cpuCount);

    /** Hands the preview surface to the render thread; null releases it (blocks until released). */
    static native void nativeSetSurface(Surface surface);

    /** @param frames 0 = specification frame counts, &gt;0 = smoke-test override (result is unofficial). */
    static native boolean nativeStart(int mode, int frames);

    static native void nativeAbort();

    static native boolean nativeIsRunning();

    /** Live status as JSON (stage, test, frame, fps, progress). */
    static native String nativeStatus();

    /** Final result JSON once a run finished or was aborted, otherwise null. */
    static native String nativeTakeResult();

    /** GL_VENDOR / GL_RENDERER / GL_VERSION as JSON once the EGL context exists. */
    static native String nativeGpuInfo();
}
