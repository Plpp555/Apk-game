/* Minimal <android/native_window_jni.h> subset (offline build only). */
#pragma once
#include <android/native_window.h>
#include <jni.h>
#ifdef __cplusplus
extern "C" {
#endif
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);
#ifdef __cplusplus
}
#endif
