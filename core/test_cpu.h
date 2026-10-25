// Preview renderer for the CPU physics test. Rendering is not part of the
// measured time (the clock is paused while the preview is drawn).
#pragma once

#include "render_common.h"
#include "sim_physics.h"

namespace tsb {

class CpuPreview {
public:
    bool init(Gpu& gpu, u32 sphereCount);
    void shutdown();
    void render(const PhysicsSim& sim, u32 frame);

private:
    Gpu* m_gpu = nullptr;
    GLuint m_prog = 0;
    GLuint m_buf = 0;
    GpuMesh m_sphere;
    u32 m_count = 0;
    void* m_staging = nullptr;
};

}  // namespace tsb
