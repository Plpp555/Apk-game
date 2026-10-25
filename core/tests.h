// Graphics test interface used by the benchmark runner.
//
// Frame pipeline (render thread), per frame f with ring slot s = f % 3:
//   end_sim(s)         wait for the worker chain of frame f, unmap slot s
//   fence wait (s+1)   the GPU finished the frame that last used slot s+1
//   begin_sim(f+1)     map slot s+1, dispatch frame f+1 to the workers
//   render(s)          issue GL commands for frame f while workers simulate f+1
// So CPU simulation of frame f+1, GL submission of frame f and GPU execution
// of frames f-1/f-2 overlap; neither side waits for the other in steady state.
#pragma once

#include "jobs.h"
#include "render_common.h"

namespace tsb {

class GraphicsTest {
public:
    virtual ~GraphicsTest() {}
    virtual const char* id() const = 0;
    virtual bool init(Gpu& gpu) = 0;
    virtual void shutdown() = 0;
    virtual void reset_sim() = 0;
    virtual void begin_sim(u32 frame, int slot, JobSystem& jobs) = 0;
    virtual void end_sim(int slot, JobSystem& jobs) = 0;
    virtual void render(u32 frame, int slot) = 0;
    virtual u64 sim_hash() const = 0;
    // Work counters for the result screen (instances / lights etc. of the last frame)
    virtual u32 last_draw_items() const = 0;
};

GraphicsTest* create_gt1_asteroids();
GraphicsTest* create_gt2_particles();
GraphicsTest* create_gt3_city();

}  // namespace tsb
