// Benchmark specification constants. Changing anything here changes the
// workload and therefore the scores and reference hashes (see docs/SPEC.md).
#pragma once

#include "base.h"

namespace tsb {

static const char* const kBenchVersion = "1.0.0";

enum BenchMode { MODE_STANDARD = 0, MODE_QUICK = 1, MODE_COUNT = 2 };

// Workload sizes
static const u32 kAsteroidCount = 32768;
static const u32 kParticleCount = 131072;
static const u32 kPhysicsCount = 24576;

// Graphics tests: frames rendered (timed) and warm-up frames (untimed)
static const u32 kGtFrames[MODE_COUNT] = {900, 300};
static const u32 kGtWarmup[MODE_COUNT] = {30, 15};

// CPU test: simulation steps for the single-thread and multi-thread runs.
// The multi-thread run also records the hash after kPhysStepsST steps; it must
// equal the single-thread hash (threading determinism self-check).
static const u32 kPhysStepsST[MODE_COUNT] = {150, 50};
static const u32 kPhysStepsMT[MODE_COUNT] = {600, 200};

// Score constants (docs/SPEC.md, "Scoring")
static const double kGraphicsScoreScale = 100.0;  // x harmonic mean FPS of GT1..GT3
static const double kCpuScoreDivisor = 2500.0;    // sphere-steps per second / divisor
static const double kWeightGraphics = 0.75;       // overall = weighted harmonic mean
static const double kWeightCpu = 0.25;

}  // namespace tsb
