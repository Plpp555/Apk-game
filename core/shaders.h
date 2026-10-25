// GLSL ES 3.00 shader sources (headers with #version/precision are prepended
// by gl::program). One source for every platform.
#pragma once

namespace tsb {
namespace shaders {

extern const char* NOISE;  // shared hash/noise functions, use as fsPrefix

extern const char* FULLSCREEN_VS;
extern const char* SPACE_SKY_FS;
extern const char* CITY_SKY_FS;

extern const char* ASTEROID_VS;
extern const char* ASTEROID_FS;
extern const char* STAR_VS;
extern const char* STAR_FS;

extern const char* PARTICLE_VS;
extern const char* PARTICLE_FS;

extern const char* SPHERE_VS;
extern const char* SPHERE_FS;

extern const char* BUILDING_VS;
extern const char* GROUND_VS;
extern const char* CITY_FS;

extern const char* BRIGHT_FS;
extern const char* DOWNSAMPLE_FS;
extern const char* BLUR_FS;
extern const char* COMPOSITE_FS;
extern const char* PRESENT_FS;

}  // namespace shaders
}  // namespace tsb
