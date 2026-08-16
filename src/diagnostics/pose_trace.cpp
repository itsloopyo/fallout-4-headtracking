// SPDX-License-Identifier: MIT

#include "pch.h"
#include "pose_trace.h"
#include "core/logging.h"

namespace Fallout4HT {
namespace {

// About 40 seconds at 100 ticks a second. Buffered rather than written per tick:
// a log write inside the camera tick would itself stall the frame and change
// what is being measured.
constexpr int kMaxRows = 4096;

struct Row {
    uint32_t atMs;
    float rawYaw;
    float rawPitch;
    float interpolatedYaw;
    float processedYaw;
    float processedPitch;
    float deltaTimeMs;
    bool newSample;
    float cameraSwingDeg;
    float appliedDeg;
    uint32_t rejected;
    uint32_t frozen;
};

// Camera-thread only apart from g_dumpRequested, so no lock: the dump runs on
// the camera thread too, at the top of the next tick after the key is pressed.
Row g_rows[kMaxRows];
int g_next = 0;
int g_filled = 0;
uint64_t g_firstMs = 0;
std::atomic<bool> g_dumpRequested{false};

void Flush() {
    const int count = g_filled;
    const int start = (g_filled == kMaxRows) ? g_next : 0;

    // Summary first, so the answer is one line rather than four thousand. Which
    // of the three stages is at fault reads straight off it: un-tracked ticks
    // means the mod stopped applying, a large worst-swing with none of them means
    // the pose itself jumped, and neither means the fault is past this point.
    int untracked = 0;
    int runs = 0;
    bool inRun = false;
    float worstSwing = 0.0f;
    // Tick-to-tick change in the HEAD's contribution, which is the number that
    // matters. Total camera swing includes the game camera turning under
    // mouse-look, and a player rounding a corner reads on it as a 6 degree jump
    // that has nothing to do with head tracking.
    float worstAppliedStep = 0.0f;
    int appliedJumps = 0;
    float previousApplied = -1.0f;
    for (int i = 0; i < count; ++i) {
        const Row& r = g_rows[(start + i) % kMaxRows];
        if (r.appliedDeg < 0.0f) {
            ++untracked;
            if (!inRun) { ++runs; inRun = true; }
            previousApplied = -1.0f;
        } else {
            inRun = false;
            if (r.cameraSwingDeg > worstSwing) worstSwing = r.cameraSwingDeg;
            if (previousApplied >= 0.0f) {
                const float step = fabsf(r.appliedDeg - previousApplied);
                if (step > worstAppliedStep) worstAppliedStep = step;
                if (step > 2.0f) ++appliedJumps;
            }
            previousApplied = r.appliedDeg;
        }
    }
    const Row& oldest = g_rows[start % kMaxRows];
    const Row& newest = g_rows[(start + count - 1) % kMaxRows];
    Log::Line("POSE TRACE SUMMARY: %d ticks over %.1f s | %d rendered un-tracked in %d runs"
              " (%.2f%%) | head-rotation jumps over 2 deg: %d (worst %.2f) | worst total"
              " camera swing %.2f deg | second-source packets +%u | dropout packets +%u",
              count, (newest.atMs - oldest.atMs) / 1000.0, untracked, runs,
              count ? 100.0 * untracked / count : 0.0, appliedJumps, worstAppliedStep,
              worstSwing, newest.rejected - oldest.rejected, newest.frozen - oldest.frozen);

    Log::Line("=== POSE TRACE: last %d ticks ===", count);
    Log::Line("ms\trawYaw\trawPitch\tinterp\tprocYaw\tprocPitch\tdt_ms\tnew\tswing\tapplied\trej\tfroz");

    for (int i = 0; i < count; ++i) {
        const Row& r = g_rows[(start + i) % kMaxRows];
        Log::Line("%u\t%+.3f\t%+.3f\t%+.3f\t%+.3f\t%+.3f\t%.2f\t%d\t%.4f\t%.3f\t%u\t%u",
                  r.atMs, r.rawYaw, r.rawPitch, r.interpolatedYaw,
                  r.processedYaw, r.processedPitch, r.deltaTimeMs,
                  r.newSample ? 1 : 0, r.cameraSwingDeg, r.appliedDeg,
                  r.rejected, r.frozen);
    }
    Log::Line("=== END POSE TRACE ===");
}

} // namespace

void DumpPoseTrace() {
    g_dumpRequested.store(true, std::memory_order_release);
}

void RecordPoseTick(const PoseTickRecord& record) {
    const uint64_t nowMs = GetTickCount64();
    if (g_firstMs == 0) g_firstMs = nowMs;

    Row& r = g_rows[g_next];
    r.atMs = static_cast<uint32_t>(nowMs - g_firstMs);
    r.rawYaw = record.rawYaw;
    r.rawPitch = record.rawPitch;
    r.interpolatedYaw = record.interpolatedYaw;
    r.processedYaw = record.processedYaw;
    r.processedPitch = record.processedPitch;
    r.deltaTimeMs = record.deltaTime * 1000.0f;
    r.newSample = record.newSample;
    r.cameraSwingDeg = record.cameraSwingDeg;
    r.appliedDeg = record.appliedDeg;
    r.rejected = static_cast<uint32_t>(record.rejectedPackets);
    r.frozen = static_cast<uint32_t>(record.frozenPackets);

    g_next = (g_next + 1) % kMaxRows;
    if (g_filled < kMaxRows) ++g_filled;

    if (g_dumpRequested.exchange(false, std::memory_order_acq_rel)) Flush();
}

} // namespace Fallout4HT
