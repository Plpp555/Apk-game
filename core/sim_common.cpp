#include "sim_common.h"

namespace tsb {

void camera_build(CameraState& c, V3 eye, V3 target, float fovY, float nearZ, float farZ) {
    c.pos = eye;
    c.fovY = fovY;
    c.aspect = kRenderAspect;
    c.nearZ = nearZ;
    c.farZ = farZ;
    c.fwd = normalize(target - eye);
    c.right = normalize(cross(c.fwd, v3(0.0f, 1.0f, 0.0f)));
    c.up = cross(c.right, c.fwd);
    c.view = m4_look_at(eye, target, v3(0.0f, 1.0f, 0.0f));
    c.proj = m4_perspective(fovY, c.aspect, nearZ, farZ);
    c.viewProj = m4_mul(c.proj, c.view);
    c.tanHalfY = dm::sin(fovY * 0.5f) / dm::cos(fovY * 0.5f);
    c.frustum = frustum_from_matrix(c.viewProj);
}

}  // namespace tsb
