// Procedural meshes, generated identically on every platform at load time.
#pragma once

#include "detmath.h"

namespace tsb {

// Interleaved vertex: position (3 floats) + normal (3 floats)
struct MeshData {
    Array<float> verts;
    Array<u16> indices;
    u32 vertex_count() const { return verts.count / 6; }
    void add_vertex(V3 p, V3 n) {
        verts.push(p.x);
        verts.push(p.y);
        verts.push(p.z);
        verts.push(n.x);
        verts.push(n.y);
        verts.push(n.z);
    }
};

// Unit icosphere (radius 1), subdiv 0..4.
void mesh_icosphere(MeshData& out, int subdiv);
// Displaced icosphere ("asteroid"), shape determined by seed; same seed gives
// the same silhouette at every subdivision level (used for LODs).
void mesh_asteroid(MeshData& out, int subdiv, u32 seed);
// Axis aligned box: x,z in [-0.5, 0.5], y in [0, 1], flat normals.
void mesh_box(MeshData& out);
// Ground plane quad of given half size at y = 0.
void mesh_plane(MeshData& out, float halfSize);

// Radial displacement used by mesh_asteroid (exposed for tests).
float asteroid_radius(V3 dir, u32 seed);

}  // namespace tsb
