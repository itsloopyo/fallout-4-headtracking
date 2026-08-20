// SPDX-License-Identifier: MIT

#include "pch.h"
#include "frame_verdict.h"
#include "core/logging.h"
#include "core/path_utils.h"
#include "core/seh_guard.h"
#include "core/vector_math.h"
#include "game/fallout4_types.h"
#include "hooks/camera_snapshot.h"
#include "hooks/player_hook.h"

#include <cstdio>

namespace Fallout4HT {
namespace {

constexpr int kTraceSize = 16384;

// Below these the two candidate states are the same state and no verdict is
// possible. Reported alongside the verdicts so a run that could not tell them
// apart never reads as a clean pass - which is exactly how four earlier
// measurements were lost.
constexpr float kMinSeparationDeg = 0.75f;
constexpr float kMinLeanUnits = 0.75f;

// The engine rebuilds the camera transforms every frame, so comparing what this
// mod wrote against what is there a moment later never matches exactly. Measured
// on this build: the recompute moves the rotation by up to 0.06 degrees and the
// position not at all. Set four times above that, and still about seventy times
// below anything a player could see - a threshold at the noise floor itself made
// the health check cry wolf once every few minutes on a run with nothing wrong
// with it, which is how a guard stops being read.
constexpr float kEngineRecomputeNoiseDeg = 0.25f;
constexpr float kEngineRecomputeNoiseUnits = 0.5f;

// A held camera that moved by this much OF THE LEAN'S OWN SIZE moved because the
// lean came off it, rather than because the game walked the camera somewhere.
constexpr float kLeanWipedFraction = 0.25f;

// What state a camera (or the matrix built from it) turned out to be in. Stored
// as a byte in the trace and printed raw, so the values are part of the trace
// file's documented format and must not be renumbered.
enum : uint8_t {
    kVerdictIndeterminate = 0,  // the two candidates were too close to tell apart
    kVerdictTracked = 1,
    kVerdictStock = 2,          // the body-aimed camera reached the renderer
    kVerdictDrifted = 3,        // moved off both candidates
};

struct Entry {
    uint64_t qpc;
    uint32_t tid;
    uint8_t path;
    uint8_t scope;
    uint8_t rotVerdict;
    uint8_t posVerdict;
    uint8_t builtVerdict; // the same question asked of the matrix that came out
    float rotToTracked;
    float rotToClean;
    float posToTracked;
    float posToClean;
    float leanUnits;
    float separationDeg;
    float pos[3];
};

Entry g_trace[kTraceSize];
std::atomic<uint32_t> g_next{0};

std::atomic<uint64_t> g_rotTracked{0};
std::atomic<uint64_t> g_rotStock{0};
std::atomic<uint64_t> g_rotIndeterminate{0};
std::atomic<uint64_t> g_rotDrifted{0};
std::atomic<uint64_t> g_posTracked{0};
std::atomic<uint64_t> g_posStock{0};
std::atomic<uint64_t> g_posIndeterminate{0};
std::atomic<uint64_t> g_posDrifted{0};
std::atomic<uint64_t> g_builtTracked{0};
std::atomic<uint64_t> g_builtStock{0};
std::atomic<uint64_t> g_pathCount[4] = {};
std::atomic<uint64_t> g_scopeCount[4] = {};

// Worst offender seen since the last report, so a rare event is not averaged
// away by thousands of good builds.
float g_worstPosMiss = 0.0f;
float g_worstRotMiss = 0.0f;

// How far apart the two candidate states were while the verdicts above were
// being reached. This is the validity gate for the whole line: at a centred head
// pose the tracked and un-tracked cameras ARE the same camera, every verdict
// reads perfect, and the run means nothing. Four measurements were lost to that
// before it was printed next to the result instead of hunted for separately.
float g_sepMax = 0.0f;
double g_sepSum = 0.0;
float g_leanMax = 0.0f;
double g_leanSum = 0.0;
uint64_t g_gateSamples = 0;

// --- what the camera tick published -----------------------------------------
uint64_t g_ticks = 0;
uint64_t g_ticksNoRotation = 0;
uint64_t g_ticksNoLean = 0;
float g_worstLeanStepM = 0.0f;
float g_worstRotStepDeg = 0.0f;
float g_prevLean[3] = {0.0f, 0.0f, 0.0f};
float g_prevApplied = 0.0f;
bool g_hasPrevTick = false;

// Read from the pause watchdog's thread, so these two are atomic where the rest
// of the tick counters above are camera-thread only.
std::atomic<uint64_t> g_totalTicks{0};
std::atomic<uint64_t> g_lastTickMs{0};

// --- integrity of the two windows the pose is applied across -----------------
uint64_t g_holds = 0;
uint64_t g_holdsDisturbedRot = 0;
uint64_t g_holdsDisturbedPos = 0;
uint64_t g_holdsLeanWiped = 0;
float g_worstHoldRot = 0.0f;
float g_worstHoldPos = 0.0f;

uint64_t g_transients = 0;
uint64_t g_transientsClobbered = 0;
float g_worstTransientRot = 0.0f;
float g_worstTransientPos = 0.0f;

// --- which camera the game is running ----------------------------------------
constexpr int kMaxStateVtablesSeen = 8;
uintptr_t g_lastStateVtable = 0;
uintptr_t g_lastState = 0;
int g_lastSlot = -1;
uintptr_t g_stateSeen[kMaxStateVtablesSeen] = {};
int g_stateSeenCount = 0;
uint64_t g_stateSwitches = 0;

// --- view builds, split by whether the pose was on the camera -----------------
std::atomic<uint64_t> g_buildsPoseOn{0};
std::atomic<uint64_t> g_buildsPoseOff{0};
std::atomic<uint64_t> g_playerBuildsPoseOff{0};

// --- the 1 Hz reporter -------------------------------------------------------
std::atomic<bool> g_reporterRunning{false};
std::thread g_reporterThread;

uint64_t Qpc() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart);
}

// Below this a vector has no usable direction, so an angle against it would be
// noise divided by noise.
constexpr float kMinLengthSquared = 1e-12f;

// Unlike the callers in the hooks, this one normalises: it is handed forward
// vectors copied out of a reference that may have been taken while the scene
// graph was mid-rebuild, so unit length cannot be assumed.
float AngleDeg(const float* a, const float* b) {
    float dot = 0.0f, la = 0.0f, lb = 0.0f;
    for (int i = 0; i < 3; ++i) {
        dot += a[i] * b[i];
        la += a[i] * a[i];
        lb += b[i] * b[i];
    }
    if (la <= kMinLengthSquared || lb <= kMinLengthSquared) return 0.0f;
    return acosf(ClampToUnitRange(dot / sqrtf(la * lb))) * RAD_TO_DEG;
}

// The camera is AT one of the two known states, or it has moved off both.
//
// Nearest-of-two is not good enough here and saying so cost a whole soak run:
// the player walking moves the camera several units and several degrees between
// the write and the build, at which point the live camera is nearer the clean
// state for no better reason than that the clean state is where it started, and
// the instrument reports the mod dropping tracking hundreds of times. Requiring
// the live state to sit within a quarter of the gap of a candidate makes the
// verdict "it IS the un-tracked camera" rather than "it leans that way", and
// leaves honest motion in its own bucket instead of in the fault count.
constexpr float kAtStateFraction = 0.25f;

uint8_t Verdict(float toTracked, float toClean, float separation, float gate) {
    if (separation < gate) return kVerdictIndeterminate;
    if (toTracked <= separation * kAtStateFraction) return kVerdictTracked;
    if (toClean <= separation * kAtStateFraction) return kVerdictStock;
    return kVerdictDrifted;
}

} // namespace

void RecordBuildVerdict(const CameraRootSnapshots& snap, OverridePath path,
                        const OverrideReference& reference) {
    static std::atomic<uint64_t> s_faults{0};
    Entry e{};
    e.qpc = Qpc();
    e.tid = GetCurrentThreadId();
    e.path = static_cast<uint8_t>(path);
    e.scope = reference.scope;
    g_pathCount[e.path & 3].fetch_add(1, std::memory_order_relaxed);
    g_scopeCount[e.scope & 3].fetch_add(1, std::memory_order_relaxed);
    if (!reference.valid) return;

    __try {
        const NiMatrix33* live = WorldRotationOf(snap.niCamera);
        const NiPoint3* livePos = WorldTranslationOf(snap.niCamera);
        const float liveFwd[3] = { live->entry[0][0], live->entry[0][1], live->entry[0][2] };
        const float pos[3] = { livePos->x, livePos->y, livePos->z };
        e.pos[0] = pos[0]; e.pos[1] = pos[1]; e.pos[2] = pos[2];

        // Orientation: which of the two rotations the camera was actually in.
        e.separationDeg = AngleDeg(reference.cleanFwd, reference.trackedFwd);
        e.rotToTracked = AngleDeg(liveFwd, reference.trackedFwd);
        e.rotToClean = AngleDeg(liveFwd, reference.cleanFwd);
        e.rotVerdict = Verdict(e.rotToTracked, e.rotToClean, e.separationDeg,
                               kMinSeparationDeg);

        // Position: the same question, which every previous instrument left
        // unasked. The user's report is a camera that jumps somewhere else while
        // still looking the same way, and that is invisible to a forward vector.
        e.leanUnits = reference.leanUnits;
        e.posToTracked = Distance3(pos, reference.trackedPos);
        e.posToClean = Distance3(pos, reference.cleanPos);
        e.posVerdict = Verdict(e.posToTracked, e.posToClean, e.leanUnits, kMinLeanUnits);

        ++g_gateSamples;
        g_sepSum += e.separationDeg;
        g_leanSum += e.leanUnits;
        if (e.separationDeg > g_sepMax) g_sepMax = e.separationDeg;
        if (e.leanUnits > g_leanMax) g_leanMax = e.leanUnits;

        switch (e.rotVerdict) {
            case kVerdictTracked: g_rotTracked.fetch_add(1, std::memory_order_relaxed); break;
            case kVerdictStock:
                g_rotStock.fetch_add(1, std::memory_order_relaxed);
                if (e.separationDeg > g_worstRotMiss) g_worstRotMiss = e.separationDeg;
                break;
            case kVerdictDrifted: g_rotDrifted.fetch_add(1, std::memory_order_relaxed); break;
            default: g_rotIndeterminate.fetch_add(1, std::memory_order_relaxed); break;
        }
        switch (e.posVerdict) {
            case kVerdictTracked: g_posTracked.fetch_add(1, std::memory_order_relaxed); break;
            case kVerdictStock:
                g_posStock.fetch_add(1, std::memory_order_relaxed);
                if (e.leanUnits > g_worstPosMiss) g_worstPosMiss = e.leanUnits;
                break;
            case kVerdictDrifted: g_posDrifted.fetch_add(1, std::memory_order_relaxed); break;
            default: g_posIndeterminate.fetch_add(1, std::memory_order_relaxed); break;
        }

        // A bad frame gets said out loud the moment it happens, with what it
        // would take to act on it. A once-a-second count tells you a fault
        // exists; it does not tell you which path produced it, and a soak run
        // that catches three events in ten minutes cannot afford to lose that.
        if (e.rotVerdict == kVerdictStock || e.posVerdict == kVerdictStock) {
            static std::atomic<uint64_t> s_said{0};
            const uint64_t n = s_said.fetch_add(1, std::memory_order_relaxed);
            if (n < 200 || (n & 0xFF) == 0) {
                Log::Line("BAD FRAME #%llu: %s%s | path %u thread %u | rot %.2f from tracked /"
                          " %.2f from clean (%.2f apart) | pos %.2f from leaned / %.2f from"
                          " un-leaned (lean %.2f units)",
                          static_cast<unsigned long long>(n + 1),
                          e.rotVerdict == kVerdictStock ? "ROTATION UN-TRACKED " : "",
                          e.posVerdict == kVerdictStock ? "LEAN MISSING" : "",
                          e.path, e.tid,
                          e.rotToTracked, e.rotToClean, e.separationDeg,
                          e.posToTracked, e.posToClean, e.leanUnits);
            }
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "frame verdict", s_faults)) {
    }

    const uint32_t slot = g_next.fetch_add(1, std::memory_order_relaxed) % kTraceSize;
    g_trace[slot] = e;
}

void RecordBuiltVerdict(const CameraRootSnapshots& snap, const OverrideReference& reference) {
    static std::atomic<uint64_t> s_faults{0};
    if (!reference.valid) return;
    __try {
        const NiMatrix44* m = reinterpret_cast<const NiMatrix44*>(
            snap.niCamera + NiCameraOffsets::WorldToCam);
        const float builtFwd[3] = { m->entry[2][0], m->entry[2][1], m->entry[2][2] };
        const float separation = AngleDeg(reference.cleanFwd, reference.trackedFwd);
        const uint8_t verdict = Verdict(AngleDeg(builtFwd, reference.trackedFwd),
                                        AngleDeg(builtFwd, reference.cleanFwd),
                                        separation, kMinSeparationDeg);
        if (verdict == kVerdictStock) {
            g_builtStock.fetch_add(1, std::memory_order_relaxed);
        } else if (verdict == kVerdictTracked) {
            g_builtTracked.fetch_add(1, std::memory_order_relaxed);
        }
        const uint32_t slot = (g_next.load(std::memory_order_relaxed) - 1) % kTraceSize;
        g_trace[slot].builtVerdict = verdict;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "built verdict", s_faults)) {
    }
}

void GetCameraTickLiveness(unsigned long long& totalTicks,
                           unsigned long long& msSinceLastTick) {
    totalTicks = g_totalTicks.load(std::memory_order_relaxed);
    const uint64_t last = g_lastTickMs.load(std::memory_order_relaxed);
    msSinceLastTick = last ? GetTickCount64() - last : 0;
}

void RecordTickPose(bool haveRotation, bool hasPosition,
                    float leanX, float leanY, float leanZ, float appliedDeg) {
    ++g_ticks;
    g_totalTicks.fetch_add(1, std::memory_order_relaxed);
    g_lastTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    if (!haveRotation) ++g_ticksNoRotation;
    if (haveRotation && !hasPosition) ++g_ticksNoLean;

    const float lean[3] = { leanX, leanY, leanZ };
    if (g_hasPrevTick) {
        const float step = Distance3(lean, g_prevLean);
        if (step > g_worstLeanStepM) g_worstLeanStepM = step;
        const float rotStep = fabsf(appliedDeg - g_prevApplied);
        if (rotStep > g_worstRotStepDeg) g_worstRotStepDeg = rotStep;
    }
    for (int i = 0; i < 3; ++i) g_prevLean[i] = lean[i];
    g_prevApplied = appliedDeg;
    g_hasPrevTick = true;
}

void RecordTransientIntegrity(float rotDeg, float posUnits) {
    ++g_transients;
    if (rotDeg > kEngineRecomputeNoiseDeg || posUnits > kEngineRecomputeNoiseUnits) {
        ++g_transientsClobbered;
        if (rotDeg > g_worstTransientRot) g_worstTransientRot = rotDeg;
        if (posUnits > g_worstTransientPos) g_worstTransientPos = posUnits;
    }
}

void RecordBuildExposure(bool poseWasOn, bool isPlayerCamera) {
    if (poseWasOn) {
        g_buildsPoseOn.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_buildsPoseOff.fetch_add(1, std::memory_order_relaxed);
    if (isPlayerCamera) g_playerBuildsPoseOff.fetch_add(1, std::memory_order_relaxed);
}

namespace {

// How far into PlayerCamera to look for the state pointer. The object is a few
// hundred bytes; this covers it without reading past anything it owns.
constexpr int kCameraStateSlotsScanned = 128;

// Which of PlayerCamera's own camera-state slots the current state sits in.
//
// The state's vtable is not enough to tell the cameras apart: VATS and first
// person share one, so a mod that keys off the vtable cannot tell "the player
// scrolled in" from "the player pressed VATS". PlayerCamera holds its states in
// an array and points at one of them, so the SLOT is the identity, and it can be
// recovered without any RVA by looking for the pointer in the object.
int CameraStateSlot(void* playerCamera, uintptr_t currentState) {
    if (playerCamera == nullptr || currentState == 0) return -1;
    // Scanned from the top of the object rather than from a known array offset:
    // the slot only has to be stable and comparable between camera modes, and a
    // qword index into PlayerCamera is both without needing an RVA that a patch
    // could move. currentState itself lives at 0x28 and is skipped.
    const uintptr_t base = reinterpret_cast<uintptr_t>(playerCamera);
    for (int slot = 0; slot < kCameraStateSlotsScanned; ++slot) {
        const uintptr_t at = base + slot * sizeof(uintptr_t);
        if (at == base + TESCameraOffsets::CurrentState) continue;
        if (*reinterpret_cast<uintptr_t*>(at) == currentState) return slot;
    }
    return -1;
}

}  // namespace

void RecordCameraState(void* playerCamera, uintptr_t currentState) {
    if (currentState == 0) return;
    static std::atomic<uint64_t> s_faults{0};
    uintptr_t stateVtable = 0;
    int slot = -1;
    __try {
        stateVtable = *reinterpret_cast<uintptr_t*>(currentState);
        slot = CameraStateSlot(playerCamera, currentState);
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "camera state", s_faults)) {
        return;
    }
    if (stateVtable == 0) return;

    if (g_lastState != 0 && currentState != g_lastState) {
        ++g_stateSwitches;
        // Said immediately rather than counted, because the states that matter
        // are the ones the camera only passes through: VATS stops the game
        // updating a moment later, so a once-a-second counter never reports the
        // transition that got it there.
        Log::Line("camera state changed: slot %d -> slot %d (vtable %llX -> %llX)",
                  g_lastSlot, slot,
                  static_cast<unsigned long long>(g_lastStateVtable),
                  static_cast<unsigned long long>(stateVtable));
    }
    g_lastState = currentState;
    g_lastSlot = slot;
    g_lastStateVtable = stateVtable;
    for (int i = 0; i < g_stateSeenCount; ++i) if (g_stateSeen[i] == stateVtable) return;
    if (g_stateSeenCount < kMaxStateVtablesSeen) g_stateSeen[g_stateSeenCount++] = stateVtable;
}

void RecordHeldIntegrity(float rotDeg, float posUnits, float leanUnits) {
    ++g_holds;
    if (rotDeg > kEngineRecomputeNoiseDeg) {
        ++g_holdsDisturbedRot;
        if (rotDeg > g_worstHoldRot) g_worstHoldRot = rotDeg;
    }
    if (posUnits > kEngineRecomputeNoiseUnits) {
        ++g_holdsDisturbedPos;
        if (posUnits > g_worstHoldPos) g_worstHoldPos = posUnits;
    }
    // A disturbance the size of the lean itself is the lean being taken off,
    // rather than the camera being moved somewhere by the game.
    if (leanUnits > kMinLeanUnits &&
        fabsf(posUnits - leanUnits) < leanUnits * kLeanWipedFraction) {
        ++g_holdsLeanWiped;
    }
}

namespace {

// Every counter the per-second report reads is cleared here, in one place, so
// the quiet path and the loud path cannot drift about what they reset.
void ClearFrameVerdictCounters() {
    g_rotTracked.store(0, std::memory_order_relaxed);
    g_rotStock.store(0, std::memory_order_relaxed);
    g_rotDrifted.store(0, std::memory_order_relaxed);
    g_rotIndeterminate.store(0, std::memory_order_relaxed);
    g_posTracked.store(0, std::memory_order_relaxed);
    g_posStock.store(0, std::memory_order_relaxed);
    g_posDrifted.store(0, std::memory_order_relaxed);
    g_posIndeterminate.store(0, std::memory_order_relaxed);
    g_builtTracked.store(0, std::memory_order_relaxed);
    g_builtStock.store(0, std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i) {
        g_pathCount[i].store(0, std::memory_order_relaxed);
        g_scopeCount[i].store(0, std::memory_order_relaxed);
    }
    g_buildsPoseOn.store(0, std::memory_order_relaxed);
    g_buildsPoseOff.store(0, std::memory_order_relaxed);
    g_playerBuildsPoseOff.store(0, std::memory_order_relaxed);
    g_holds = 0;
    g_holdsDisturbedRot = 0;
    g_holdsDisturbedPos = 0;
    g_holdsLeanWiped = 0;
    g_worstHoldRot = 0.0f;
    g_worstHoldPos = 0.0f;
    g_transients = 0;
    g_transientsClobbered = 0;
    g_worstTransientRot = 0.0f;
    g_worstTransientPos = 0.0f;
    g_ticks = 0;
    g_ticksNoRotation = 0;
    g_ticksNoLean = 0;
    g_worstLeanStepM = 0.0f;
    g_worstRotStepDeg = 0.0f;
    g_stateSwitches = 0;
    g_worstPosMiss = 0.0f;
    g_worstRotMiss = 0.0f;
    g_sepMax = 0.0f;
    g_leanMax = 0.0f;
    g_sepSum = 0.0;
    g_leanSum = 0.0;
    g_gateSamples = 0;
}

// Rolling totals for the healthy case, so a quiet session leaves one line a
// minute in the log instead of seven a second.
uint64_t g_minuteBuilds = 0;
uint64_t g_minutePoseOff = 0;
uint64_t g_minuteTicks = 0;
float g_minuteSepMax = 0.0f;
float g_minuteLeanMax = 0.0f;
int g_quietSeconds = 0;

// How many consecutive healthy seconds earn the one-line summary.
constexpr int kHealthySummarySeconds = 60;

// A session that is broken stays broken, and the eight-line block costs about
// 2 KB a second - 7 MB an hour of the same numbers, which buries the startup
// chain in the file the player is asked to send. The onset is what diagnoses
// the fault, so the first minute prints every second and after that one second
// a minute does.
uint64_t g_unhealthySeconds = 0;
constexpr uint64_t kUnhealthyDetailSeconds = 60;

// Start the healthy run over. Called from all three exits of the reporter -
// nothing happening, the summary having just been printed, and any unhealthy
// second - and spelling it out at each was three chances for one of them to keep
// a stale total and let a summary describe a period it did not cover.
void ResetHealthyRun() {
    g_quietSeconds = 0;
    g_minuteBuilds = 0;
    g_minutePoseOff = 0;
    g_minuteTicks = 0;
    g_minuteSepMax = 0.0f;
    g_minuteLeanMax = 0.0f;
}

// Defined below the reporter, next to nothing but the counters it prints.
void LogUnhealthySecond();

}  // namespace

void ReportFrameVerdictsOnce() {
    // Everything is read once, here, because reading a counter clears it: the
    // health decision and the line that reports it have to see the same numbers.
    const uint64_t rotStock = g_rotStock.load(std::memory_order_relaxed);
    const uint64_t posStock = g_posStock.load(std::memory_order_relaxed);
    const uint64_t builtStock = g_builtStock.load(std::memory_order_relaxed);
    const uint64_t poseOn = g_buildsPoseOn.load(std::memory_order_relaxed);
    const uint64_t poseOff = g_buildsPoseOff.load(std::memory_order_relaxed);
    const uint64_t playerOff = g_playerBuildsPoseOff.load(std::memory_order_relaxed);
    const uint64_t foreignOff = poseOff > playerOff ? poseOff - playerOff : 0;

    // A render pass that samples the camera while the head pose is off it lights
    // its frame from the body-aimed camera. That, not the camera state, was the
    // flicker, so it is the first thing this asks about.
    const bool healthy = rotStock == 0 && posStock == 0 && builtStock == 0
                      && foreignOff == 0 && g_holdsLeanWiped == 0
                      && g_transientsClobbered == 0 && g_ticksNoLean == 0;

    // Nothing happening is not health. At the main menu, on a loading screen and
    // between saves the camera does not tick and no view is built, and a line
    // claiming everything is fine reads exactly like one from a working session -
    // which is the whole failure mode this instrument exists to refuse.
    if (poseOn + poseOff == 0 && g_ticks == 0) {
        ClearFrameVerdictCounters();
        ResetHealthyRun();
        g_unhealthySeconds = 0;
        return;
    }

    if (healthy) {
        g_unhealthySeconds = 0;
        // Accumulated only on healthy seconds, so the totals belong to the run
        // the line claims to describe. Folding an unhealthy second into them
        // would let a summary say "all the player's own" about a period that
        // contained foreign passes, which is the sort of quietly false clean
        // reading this whole instrument exists to stop producing.
        g_minuteBuilds += poseOn + poseOff;
        g_minutePoseOff += poseOff;
        g_minuteTicks += g_ticks;
        if (g_sepMax > g_minuteSepMax) g_minuteSepMax = g_sepMax;
        if (g_leanMax > g_minuteLeanMax) g_minuteLeanMax = g_leanMax;
        ++g_quietSeconds;
        if (g_quietSeconds >= kHealthySummarySeconds) {
            Log::Line("head tracking healthy for %ds: %llu camera ticks, %llu view builds,"
                      " %llu of them while the pose was off (all the player's own),"
                      " 0 un-tracked frames, 0 lost leans | applied up to %.1f deg / %.1f units",
                      g_quietSeconds,
                      static_cast<unsigned long long>(g_minuteTicks),
                      static_cast<unsigned long long>(g_minuteBuilds),
                      static_cast<unsigned long long>(g_minutePoseOff),
                      g_minuteSepMax, g_minuteLeanMax);
            ResetHealthyRun();
        }
        ClearFrameVerdictCounters();
        return;
    }
    ResetHealthyRun();
    ++g_unhealthySeconds;
    if (g_unhealthySeconds <= kUnhealthyDetailSeconds ||
        g_unhealthySeconds % kUnhealthyDetailSeconds == 0) {
        LogUnhealthySecond();
    }
    ClearFrameVerdictCounters();
}

namespace {

// The full block, printed only on a second that had something wrong with it. Ten
// lines a second is unreadable as a steady state, which is why the healthy path
// above collapses to one line a minute instead.
void LogUnhealthySecond() {
    Log::Line("  FRAME VERDICT rotation: %llu tracked, %llu STOCK (worst %.1f deg apart),"
              " %llu moved off both, %llu indeterminate | built matrix: %llu tracked, %llu STOCK",
              static_cast<unsigned long long>(g_rotTracked.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_rotStock.load(std::memory_order_relaxed)),
              g_worstRotMiss,
              static_cast<unsigned long long>(
                  g_rotDrifted.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(
                  g_rotIndeterminate.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(
                  g_builtTracked.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_builtStock.load(std::memory_order_relaxed)));
    Log::Line("  FRAME VERDICT position: %llu leaned, %llu NOT LEANED (worst lean %.1f units),"
              " %llu moved off both, %llu indeterminate"
              " | paths: none %llu held %llu transient %llu nested %llu"
              " | in: camera tick %llu, CLEAN GAMEPLAY SCOPE %llu, elsewhere %llu",
              static_cast<unsigned long long>(g_posTracked.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_posStock.load(std::memory_order_relaxed)),
              g_worstPosMiss,
              static_cast<unsigned long long>(
                  g_posDrifted.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(
                  g_posIndeterminate.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_pathCount[0].load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_pathCount[1].load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_pathCount[2].load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_pathCount[3].load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_scopeCount[1].load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_scopeCount[2].load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_scopeCount[3].load(std::memory_order_relaxed)));
    Log::Line("  FRAME VERDICT gate: head rotation applied mean %.2f deg / max %.2f deg,"
              " lean applied mean %.2f units / max %.2f units%s",
              g_gateSamples ? g_sepSum / g_gateSamples : 0.0, g_sepMax,
              g_gateSamples ? g_leanSum / g_gateSamples : 0.0, g_leanMax,
              (g_sepMax < kMinSeparationDeg && g_leanMax < kMinLeanUnits)
                  ? "  <-- NOTHING APPLIED, the verdicts above mean nothing" : "");
    Log::Line("  HELD CAMERA: %llu holds, %llu disturbed in rotation (worst %.2f deg),"
              " %llu moved (worst %.2f units), %llu of those look like THE LEAN BEING WIPED",
              static_cast<unsigned long long>(g_holds),
              static_cast<unsigned long long>(g_holdsDisturbedRot), g_worstHoldRot,
              static_cast<unsigned long long>(g_holdsDisturbedPos), g_worstHoldPos,
              static_cast<unsigned long long>(g_holdsLeanWiped));
    {
        char states[160];
        int written = 0;
        for (int i = 0; i < g_stateSeenCount && written >= 0 && written < (int)sizeof(states); ++i) {
            written += snprintf(states + written, sizeof(states) - written, " %llX",
                                static_cast<unsigned long long>(g_stateSeen[i]));
        }
        Log::Line("  CAMERA STATE: slot %d (vtable %llX), %llu switches this second, seen so far:%s",
                  g_lastSlot,
                  static_cast<unsigned long long>(g_lastStateVtable),
                  static_cast<unsigned long long>(g_stateSwitches), states);
    }

    {
        const unsigned long long on = g_buildsPoseOn.load(std::memory_order_relaxed);
        const unsigned long long off = g_buildsPoseOff.load(std::memory_order_relaxed);
        Log::Line("  EXPOSURE: %llu view builds with the head pose ON the camera,"
                  " %llu WITH IT OFF (%.2f%%, %llu of them the player camera)",
                  on, off, (on + off) ? 100.0 * off / (on + off) : 0.0,
                  static_cast<unsigned long long>(
                      g_playerBuildsPoseOff.load(std::memory_order_relaxed)));
    }

    Log::Line("  OVERRIDE WINDOW: %llu builds wrapped, %llu where the engine wrote the camera"
              " during the build and the restore threw it away (worst %.2f deg, %.2f units)",
              static_cast<unsigned long long>(g_transients),
              static_cast<unsigned long long>(g_transientsClobbered),
              g_worstTransientRot, g_worstTransientPos);

    Log::Line("  POSE PUBLISHED: %llu ticks, %llu with no rotation, %llu with rotation but"
              " NO LEAN | worst step between ticks: %.2f units of lean, %.2f deg of rotation",
              static_cast<unsigned long long>(g_ticks),
              static_cast<unsigned long long>(g_ticksNoRotation),
              static_cast<unsigned long long>(g_ticksNoLean),
              g_worstLeanStepM * kUnitsPerMeter, g_worstRotStepDeg);
}

}  // namespace

void StartFrameVerdictReporter() {
    if (g_reporterRunning.exchange(true)) return;
    g_reporterThread = std::thread([] {
        while (g_reporterRunning.load(std::memory_order_relaxed)) {
            Sleep(1000);
            if (!g_reporterRunning.load(std::memory_order_relaxed)) break;
            ReportFrameVerdictsOnce();
        }
    });
}

void StopFrameVerdictReporter() {
    if (!g_reporterRunning.exchange(false)) return;
    if (g_reporterThread.joinable()) g_reporterThread.join();
}

void DumpFrameVerdictTrace() {
    const std::string path = GetModulePath("HeadTracking.verdict.txt");
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        Log::Line("verdict trace: cannot open %s", path.c_str());
        return;
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const uint32_t total = g_next.load(std::memory_order_relaxed);
    const uint32_t count = total < kTraceSize ? total : kTraceSize;
    const uint32_t start = total < kTraceSize ? 0 : total % kTraceSize;

    fprintf(f, "path: 0 none 1 held 2 transient 3 nested | verdict: 0 indeterminate"
               " 1 tracked 2 STOCK 3 drifted off both\n");
    fprintf(f, "%8s %5s %4s %4s %4s %7s %9s %9s %9s %9s %8s %8s %12s %12s %12s\n",
            "ms", "pth.sc", "rot", "pos", "blt", "tid", "rot>trk", "rot>cln", "pos>trk",
            "pos>cln", "lean", "sep", "x", "y", "z");

    uint64_t first = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const Entry& e = g_trace[(start + i) % kTraceSize];
        if (first == 0) first = e.qpc;
        const double ms = static_cast<double>(e.qpc - first) * 1000.0 /
                          static_cast<double>(freq.QuadPart);
        fprintf(f, "%8.2f %5u %4u %4u %4u %7u %9.3f %9.3f %9.2f %9.2f %8.2f %8.2f"
                   " %12.2f %12.2f %12.2f\n",
                ms, e.path * 10u + e.scope, e.rotVerdict, e.posVerdict, e.builtVerdict, e.tid,
                e.rotToTracked, e.rotToClean, e.posToTracked, e.posToClean,
                e.leanUnits, e.separationDeg, e.pos[0], e.pos[1], e.pos[2]);
    }
    fclose(f);
    Log::Line("verdict trace: wrote %u builds to %s", count, path.c_str());
}

} // namespace Fallout4HT
