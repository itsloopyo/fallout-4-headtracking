// SPDX-License-Identifier: MIT

#include "pch.h"
#include "render_audit.h"
#include "core/logging.h"
#include "core/mod.h"
#include "core/seh_guard.h"
#include "core/vector_math.h"
#include "game/fallout4_types.h"
#include "hooks/camera_hook.h"
#include "hooks/camera_snapshot.h"
#include "hooks/view_matrix_hook.h"

namespace Fallout4HT {
namespace {

// Long enough to play through the flicker rather than to catch it by luck: the
// point of arming this is to have it running WHILE the view misbehaves.
constexpr uint64_t kAuditDurationMs = 90000;
constexpr uint64_t kWindowMs = 1000;

// About 0.5 degrees. Tight enough that the head rotation (which is what
// separates the two candidates) cannot be mistaken for a match on the other one.
constexpr float kMatchCos = 0.99995f;

// A frame-to-frame view movement this large is not a hand on the mouse.
constexpr float kJumpDeg = 2.0f;
// Below this a movement is too small to carry a meaningful direction, so it is
// not counted when looking for back-and-forth reversals.
constexpr float kReversalFloorDeg = 1.0f;

// Read live at the top of a camera tick, before the engine updates anything.
struct LiveCamera {
    NiPoint3 rendered;   // worldToCam row 2 - the forward the last frame was drawn with
    NiPoint3 forward;    // cameraRoot.world row 1
    NiPoint3 position;   // cameraRoot.world translate
};

float Dot(const NiPoint3& a, const NiPoint3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

NiPoint3 Normalized(const NiPoint3& v) {
    const float len = sqrtf(Dot(v, v));
    if (len <= 1e-6f) return NiPoint3();
    return NiPoint3(v.x / len, v.y / len, v.z / len);
}

NiPoint3 RowOf(const float (&rows)[3][4], int row) {
    return NiPoint3(rows[row][0], rows[row][1], rows[row][2]);
}

float AngleBetweenDeg(const NiPoint3& a, const NiPoint3& b) {
    return DegreesBetweenUnit(&a.x, &b.x);
}

bool ReadLiveCamera(void* thisCamera, LiveCamera& out) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        CameraNodes nodes{};
        if (!ResolveCameraNodes(thisCamera, nodes)) return false;

        const NiMatrix44* worldToCam = reinterpret_cast<const NiMatrix44*>(
            nodes.niCamera + NiCameraOffsets::WorldToCam);
        out.rendered = Normalized(NiPoint3(worldToCam->entry[2][0], worldToCam->entry[2][1],
                                           worldToCam->entry[2][2]));

        const NiMatrix33* rootWorld = WorldRotationOf(nodes.cameraRoot);
        out.forward = Normalized(RowOf(rootWorld->entry, 1));
        out.position = *WorldTranslationOf(nodes.cameraRoot);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "render audit", s_faults)) {
    }
    return false;
}

enum class Match { Tracked, Clean, Other };

// Signs are taken as absolute: worldToCam's forward row may be negated relative
// to the scene-graph row depending on the view convention, and that flip applies
// to the tracked and the clean candidate alike.
Match Classify(const NiPoint3& live, const NiPoint3& tracked, const NiPoint3& clean) {
    const float dt = fabsf(Dot(live, tracked));
    const float dc = fabsf(Dot(live, clean));
    if (dt >= dc && dt > kMatchCos) return Match::Tracked;
    if (dc > kMatchCos) return Match::Clean;
    return Match::Other;
}

struct Tally {
    uint32_t tracked;
    uint32_t clean;
    uint32_t other;

    void Add(Match m) {
        switch (m) {
            case Match::Tracked: ++tracked; break;
            case Match::Clean:   ++clean;   break;
            case Match::Other:   ++other;   break;
        }
    }
};

// Frame-to-frame movement of one axis. A view being fought over swings back and
// forth, so the reversal count is what separates a flicker from a hand on the
// mouse: panning moves the view the same way tick after tick.
struct Motion {
    NiPoint3 previous;
    NiPoint3 previousDelta;
    bool hasPrevious;
    bool hasPreviousDelta;
    float maxJumpDeg;
    uint32_t jumps;
    uint32_t reversals;

    void Add(const NiPoint3& axis) {
        if (hasPrevious) {
            const float deg = AngleBetweenDeg(previous, axis);
            if (deg > maxJumpDeg) maxJumpDeg = deg;
            if (deg > kJumpDeg) ++jumps;

            const NiPoint3 delta(axis.x - previous.x, axis.y - previous.y, axis.z - previous.z);
            if (deg > kReversalFloorDeg) {
                if (hasPreviousDelta && Dot(delta, previousDelta) < 0.0f) ++reversals;
                previousDelta = delta;
                hasPreviousDelta = true;
            }
        }
        previous = axis;
        hasPrevious = true;
    }

    // Keeps `previous` so a window boundary does not fabricate a gap in the
    // movement history.
    void ResetCounters() {
        maxJumpDeg = 0.0f;
        jumps = 0;
        reversals = 0;
    }
};

// Same idea for the camera position, in engine units (~70 per metre).
struct PositionMotion {
    NiPoint3 previous;
    bool hasPrevious;
    float maxJumpUnits;

    void Add(const NiPoint3& p) {
        if (hasPrevious) {
            const NiPoint3 d(p.x - previous.x, p.y - previous.y, p.z - previous.z);
            const float len = sqrtf(Dot(d, d));
            if (len > maxJumpUnits) maxJumpUnits = len;
        }
        previous = p;
        hasPrevious = true;
    }

    void ResetCounters() { maxJumpUnits = 0.0f; }
};

// How far apart consecutive camera ticks land. The mod applies head tracking on
// every tick, so if the engine ticks the player camera twice per rendered frame
// (which the per-frame pipeline cache in mod.cpp already assumes it can), a
// moving head pose gets applied twice from two different poses, and which one
// the renderer catches decides what that frame looks like. Tight pairs followed
// by a frame-length gap are that signature.
struct TickIntervals {
    uint64_t count;
    uint64_t under1ms;
    uint64_t under3ms;
    double minMs;
    double maxMs;
    double totalMs;

    void Add(double ms) {
        ++count;
        if (ms < 1.0) ++under1ms;
        if (ms < 3.0) ++under3ms;
        if (count == 1 || ms < minMs) minMs = ms;
        if (ms > maxMs) maxMs = ms;
        totalMs += ms;
    }
};

TickIntervals g_intervals{};
LARGE_INTEGER g_lastTickQpc{};
LARGE_INTEGER g_qpcFreq{};

// Written from the hotkey thread, read on the camera thread. Everything else
// below is camera-thread only.
std::atomic<uint64_t> g_auditUntilMs{0};

// Where the game camera is pointing, which is the axis the flicker tracks: it
// appears in a band around the horizon and goes away past roughly +-30 degrees.
// Rows are the world axes and world Z is up, so the forward row's z is sin(pitch).
float g_cameraPitchDeg = 0.0f;

uint64_t g_windowStartMs = 0;
uint32_t g_ticks = 0;
uint32_t g_trackedTicks = 0;
Tally g_rendered{};
Motion g_renderedMotion{};
Motion g_cleanMotion{};
PositionMotion g_positionMotion{};
float g_maxAppliedDeg = 0.0f;

void Report(uint64_t nowMs) {
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    Mod::Instance().GetLastRotation(yaw, pitch, roll);

    Log::Line("render audit %.1fs: %u ticks (%u tracked) | rendered tracked=%u clean=%u other=%u"
              " | max frame-to-frame swing: rendered %.2f deg (jumps %u, reversals %u),"
              " camera-forward %.2f deg (jumps %u, reversals %u), camera-pos %.2f units"
              " | camera pitch %+.1f deg | head ypr=(%+.1f %+.1f %+.1f) applied %.2f deg",
              (nowMs - g_windowStartMs) / 1000.0, g_ticks, g_trackedTicks,
              g_rendered.tracked, g_rendered.clean, g_rendered.other,
              g_renderedMotion.maxJumpDeg, g_renderedMotion.jumps, g_renderedMotion.reversals,
              g_cleanMotion.maxJumpDeg, g_cleanMotion.jumps, g_cleanMotion.reversals,
              g_positionMotion.maxJumpUnits,
              g_cameraPitchDeg, yaw, pitch, roll, g_maxAppliedDeg);

    ReportViewMatrixStats();

    Log::Line("  tick spacing: %llu gaps, min %.2f ms, mean %.2f ms, max %.2f ms,"
              " under 1 ms=%llu, under 3 ms=%llu",
              static_cast<unsigned long long>(g_intervals.count),
              g_intervals.minMs,
              g_intervals.count ? g_intervals.totalMs / g_intervals.count : 0.0,
              g_intervals.maxMs,
              static_cast<unsigned long long>(g_intervals.under1ms),
              static_cast<unsigned long long>(g_intervals.under3ms));
    g_intervals = TickIntervals{};

    g_windowStartMs = 0;
    g_ticks = 0;
    g_trackedTicks = 0;
    g_rendered = Tally{};
    g_renderedMotion.ResetCounters();
    g_cleanMotion.ResetCounters();
    g_positionMotion.ResetCounters();
    g_maxAppliedDeg = 0.0f;
}

// --- sub-frame sampler -------------------------------------------------------
// Reads the two rotations the renderer consumes at ~50 kHz. A hook that swaps a
// matrix out and back inside one frame (the fire path and the auto-aim solver
// both do exactly that) leaves a window the per-tick audit steps straight over,
// and the renderer can land in it. Counting samples by which rotation was
// present measures how wide those windows really are.
struct SampleTally {
    uint64_t total;
    uint64_t tracked;
    uint64_t clean;
    uint64_t other;

    void Add(Match m) {
        ++total;
        switch (m) {
            case Match::Tracked: ++tracked; break;
            case Match::Clean:   ++clean;   break;
            case Match::Other:   ++other;   break;
        }
    }
};

// Running min/max per component. A quantity that alternates between two states
// shows up as a range far wider than the noise floor, whichever of them a given
// sample happens to catch - which is what a once-per-frame reader cannot see.
template <int N>
struct Spread {
    float lo[N];
    float hi[N];
    bool seeded;

    void Add(const float* v) {
        if (!seeded) {
            for (int i = 0; i < N; ++i) { lo[i] = v[i]; hi[i] = v[i]; }
            seeded = true;
            return;
        }
        for (int i = 0; i < N; ++i) {
            if (v[i] < lo[i]) lo[i] = v[i];
            if (v[i] > hi[i]) hi[i] = v[i];
        }
    }

    float Widest() const {
        float widest = 0.0f;
        if (!seeded) return widest;
        for (int i = 0; i < N; ++i) {
            const float range = hi[i] - lo[i];
            if (range > widest) widest = range;
        }
        return widest;
    }
};

struct CameraSpreads {
    Spread<12> rootRot;
    Spread<3>  rootPos;
    Spread<12> niCamRot;
    Spread<3>  niCamPos;
    Spread<12> niCamLocalRot;
    Spread<16> worldToCam;
};

bool SampleSpreads(const CameraRootSnapshots& snap, CameraSpreads& out) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        out.rootRot.Add(&WorldRotationOf(snap.cameraRoot)->entry[0][0]);
        out.rootPos.Add(&WorldTranslationOf(snap.cameraRoot)->x);
        out.niCamRot.Add(&WorldRotationOf(snap.niCamera)->entry[0][0]);
        out.niCamPos.Add(&WorldTranslationOf(snap.niCamera)->x);
        out.niCamLocalRot.Add(&LocalRotationOf(snap.niCamera)->entry[0][0]);
        out.worldToCam.Add(&reinterpret_cast<const NiMatrix44*>(
            snap.niCamera + NiCameraOffsets::WorldToCam)->entry[0][0]);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "render spreads", s_faults)) {
    }
    return false;
}

// cameraRoot remains clean because Fallout uses it for collision and VATS.
// niCamera and worldToCam stay tracked except inside synchronized gameplay scopes.
struct ExposureWindows {
    uint64_t count;
    double totalUs;
    double maxUs;
    bool open;
    LARGE_INTEGER openedAt;

    void Sample(bool isClean, const LARGE_INTEGER& now, const LARGE_INTEGER& freq) {
        if (isClean && !open) {
            open = true;
            openedAt = now;
            return;
        }
        if (!isClean && open) {
            open = false;
            const double us = 1e6 * static_cast<double>(now.QuadPart - openedAt.QuadPart)
                            / static_cast<double>(freq.QuadPart);
            ++count;
            totalUs += us;
            if (us > maxUs) maxUs = us;
        }
    }
};

// Every out-param is a reference: three of these were pointers and three were
// references for no reason anyone could name, which at a seven-argument call site
// is an invitation to pass a tally where a window belongs.
bool SampleOnce(const CameraRootSnapshots& snap, SampleTally& root, SampleTally& niCam,
                SampleTally& w2c, ExposureWindows& rootWindows, ExposureWindows& niCamWindows,
                ExposureWindows& w2cWindows, const LARGE_INTEGER& freq) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const NiPoint3 rootForward = Normalized(RowOf(WorldRotationOf(snap.cameraRoot)->entry, 1));
        // niCamera's rows are forward, up, right - a permutation of cameraRoot's.
        const NiPoint3 niCamForward = Normalized(RowOf(WorldRotationOf(snap.niCamera)->entry, 0));

        const Match rootMatch = Classify(rootForward,
                                         Normalized(RowOf(snap.trackedWorld, 1)),
                                         Normalized(RowOf(snap.cleanWorld, 1)));
        const Match niCamMatch = Classify(niCamForward,
                                          Normalized(RowOf(snap.trackedNiCamWorld, 0)),
                                          Normalized(RowOf(snap.cleanNiCamWorld, 0)));

        // worldToCam is what the renderer consumes. Classify it against the
        // renderer-facing niCamera rather than the clean gameplay cameraRoot.
        const NiMatrix44* worldToCam = reinterpret_cast<const NiMatrix44*>(
            snap.niCamera + NiCameraOffsets::WorldToCam);
        const Match w2cMatch = Classify(
            Normalized(NiPoint3(worldToCam->entry[2][0], worldToCam->entry[2][1],
                                worldToCam->entry[2][2])),
            Normalized(RowOf(snap.trackedNiCamWorld, 0)),
            Normalized(RowOf(snap.cleanNiCamWorld, 0)));

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        rootWindows.Sample(rootMatch == Match::Clean, now, freq);
        niCamWindows.Sample(niCamMatch == Match::Clean, now, freq);
        w2cWindows.Sample(w2cMatch == Match::Clean, now, freq);

        root.Add(rootMatch);
        niCam.Add(niCamMatch);
        w2c.Add(w2cMatch);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "render sampler", s_faults)) {
    }
    return false;
}

void SamplerThread(uint64_t untilMs) {
    SampleTally root{};
    SampleTally niCam{};
    CameraSpreads spreads{};
    SampleTally w2c{};
    ExposureWindows rootWindows{};
    ExposureWindows niCamWindows{};
    ExposureWindows w2cWindows{};
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const uint64_t startedMs = GetTickCount64();
    uint64_t snapshotMisses = 0;
    uint64_t lastReportMs = startedMs;
    uint64_t reportedRoot = 0, reportedNiCam = 0, reportedW2c = 0;
    double reportedRootUs = 0.0, reportedNiCamUs = 0.0, reportedW2cUs = 0.0;

    while (GetTickCount64() < untilMs) {
        // Re-read the snapshot every millisecond or so: it only changes once per
        // frame, and fetching it per sample would starve the camera thread's
        // publishes through the seqlock.
        CameraRootSnapshots snap;
        if (!GetCameraRootSnapshots(snap) || snap.niCamera == 0) {
            ++snapshotMisses;
            Sleep(1);
            continue;
        }

        // Report every second so a long recording identifies when state changed.
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastReportMs >= 1000) {
            Log::Line("  sub-frame: cameraRoot clean (expected) %llu windows / %.1f us mean,"
                      " niCamera clean %llu / %.1f us, worldToCam clean %llu / %.1f us",
                      static_cast<unsigned long long>(rootWindows.count - reportedRoot),
                      rootWindows.count > reportedRoot
                          ? (rootWindows.totalUs - reportedRootUs) / (rootWindows.count - reportedRoot) : 0.0,
                      static_cast<unsigned long long>(niCamWindows.count - reportedNiCam),
                      niCamWindows.count > reportedNiCam
                          ? (niCamWindows.totalUs - reportedNiCamUs) / (niCamWindows.count - reportedNiCam) : 0.0,
                      static_cast<unsigned long long>(w2cWindows.count - reportedW2c),
                      w2cWindows.count > reportedW2c
                          ? (w2cWindows.totalUs - reportedW2cUs) / (w2cWindows.count - reportedW2c) : 0.0);
            reportedRoot = rootWindows.count;   reportedRootUs = rootWindows.totalUs;
            reportedNiCam = niCamWindows.count; reportedNiCamUs = niCamWindows.totalUs;
            reportedW2c = w2cWindows.count;     reportedW2cUs = w2cWindows.totalUs;
            lastReportMs = nowMs;
        }

        const uint64_t sliceEnd = GetTickCount64() + 1;
        while (GetTickCount64() < sliceEnd) {
            if (!SampleOnce(snap, root, niCam, w2c, rootWindows, niCamWindows,
                            w2cWindows, freq)) break;
            if (!SampleSpreads(snap, spreads)) break;
            for (int i = 0; i < 8; ++i) YieldProcessor();
        }
    }

    const double elapsedMs = static_cast<double>(GetTickCount64() - startedMs);
    Log::Line("RENDER CAMERA EXPOSURE over %.1fs | cameraRoot clean (expected): %llu windows,"
              " %.1f us mean, %.1f us max, %.3f%% of the time | niCamera clean: %llu windows, %.1f us mean,"
              " %.1f us max, %.3f%% of the time",
              elapsedMs / 1000.0,
              static_cast<unsigned long long>(rootWindows.count),
              rootWindows.count ? rootWindows.totalUs / rootWindows.count : 0.0,
              rootWindows.maxUs,
              elapsedMs > 0 ? 100.0 * rootWindows.totalUs / (elapsedMs * 1000.0) : 0.0,
              static_cast<unsigned long long>(niCamWindows.count),
              niCamWindows.count ? niCamWindows.totalUs / niCamWindows.count : 0.0,
              niCamWindows.maxUs,
              elapsedMs > 0 ? 100.0 * niCamWindows.totalUs / (elapsedMs * 1000.0) : 0.0);

    Log::Line("  CLEAN VIEW LEAKS in worldToCam: %llu windows, %.1f us mean,"
              " %.1f us max, %.3f%% of the time | samples tracked=%llu clean=%llu other=%llu",
              static_cast<unsigned long long>(w2cWindows.count),
              w2cWindows.count ? w2cWindows.totalUs / w2cWindows.count : 0.0,
              w2cWindows.maxUs,
              elapsedMs > 0 ? 100.0 * w2cWindows.totalUs / (elapsedMs * 1000.0) : 0.0,
              static_cast<unsigned long long>(w2c.tracked),
              static_cast<unsigned long long>(w2c.clean),
              static_cast<unsigned long long>(w2c.other));

    Log::Line("sub-frame spread (widest range any component reached):"
              " cameraRoot rot=%.5f pos=%.3f | niCamera rot=%.5f pos=%.3f local-rot=%.5f"
              " | worldToCam=%.3f",
              spreads.rootRot.Widest(), spreads.rootPos.Widest(),
              spreads.niCamRot.Widest(), spreads.niCamPos.Widest(),
              spreads.niCamLocalRot.Widest(), spreads.worldToCam.Widest());

    Log::Line("sub-frame sampler: %llu samples | cameraRoot tracked=%llu clean=%llu other=%llu"
              " | niCamera tracked=%llu clean=%llu other=%llu | snapshot misses=%llu",
              static_cast<unsigned long long>(root.total),
              static_cast<unsigned long long>(root.tracked),
              static_cast<unsigned long long>(root.clean),
              static_cast<unsigned long long>(root.other),
              static_cast<unsigned long long>(niCam.tracked),
              static_cast<unsigned long long>(niCam.clean),
              static_cast<unsigned long long>(niCam.other),
              static_cast<unsigned long long>(snapshotMisses));
}

} // namespace

void ArmRenderAudit() {
    const uint64_t until = GetTickCount64() + kAuditDurationMs;
    std::thread(SamplerThread, until).detach();
    g_auditUntilMs.store(GetTickCount64() + kAuditDurationMs);
    g_renderedMotion = Motion{};
    g_cleanMotion = Motion{};
    g_positionMotion = PositionMotion{};
    g_intervals = TickIntervals{};
    g_lastTickQpc = LARGE_INTEGER{};
    Log::Line("render audit armed for %llu s",
              static_cast<unsigned long long>(kAuditDurationMs / 1000));
}

void AuditRenderedFrame(void* thisCamera) {
    const uint64_t nowMs = GetTickCount64();
    if (nowMs > g_auditUntilMs.load()) {
        if (g_windowStartMs != 0) Report(nowMs);
        return;
    }

    LiveCamera live{};
    if (!ReadLiveCamera(thisCamera, live)) return;

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    if (g_qpcFreq.QuadPart == 0) QueryPerformanceFrequency(&g_qpcFreq);
    if (g_lastTickQpc.QuadPart != 0) {
        g_intervals.Add(1000.0 * static_cast<double>(qpc.QuadPart - g_lastTickQpc.QuadPart)
                        / static_cast<double>(g_qpcFreq.QuadPart));
    }
    g_lastTickQpc = qpc;

    if (g_windowStartMs == 0) g_windowStartMs = nowMs;
    ++g_ticks;
    g_renderedMotion.Add(live.rendered);
    g_cleanMotion.Add(live.forward);
    g_positionMotion.Add(live.position);
    g_cameraPitchDeg = asinf(ClampToUnitRange(live.forward.z)) * RAD_TO_DEG;

    // Classification needs the previous tick's before/after pair, which only
    // exists while tracking is actually running. The swing measurements above do
    // not, which is what makes a tracking-off baseline possible.
    CameraRootSnapshots snap;
    if (GetCameraRootSnapshots(snap) && snap.niCamera != 0) {
        ++g_trackedTicks;
        const NiPoint3 tracked = Normalized(RowOf(snap.trackedWorld, 1));
        const NiPoint3 clean = Normalized(RowOf(snap.cleanWorld, 1));
        g_rendered.Add(Classify(live.rendered, tracked, clean));

        const float appliedDeg = AngleBetweenDeg(tracked, clean);
        if (appliedDeg > g_maxAppliedDeg) g_maxAppliedDeg = appliedDeg;
    }

    if (nowMs - g_windowStartMs >= kWindowMs) Report(nowMs);
}

} // namespace Fallout4HT
