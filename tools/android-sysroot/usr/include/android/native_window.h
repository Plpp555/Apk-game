/* Minimal <android/native_window.h> subset (offline build only). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_acquire(ANativeWindow* window);
void ANativeWindow_release(ANativeWindow* window);
int32_t ANativeWindow_getWidth(ANativeWindow* window);
int32_t ANativeWindow_getHeight(ANativeWindow* window);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow* window, int32_t width, int32_t height, int32_t format);
#ifdef __cplusplus
}
#endif
