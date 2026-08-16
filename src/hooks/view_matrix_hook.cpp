// SPDX-License-Identifier: MIT

#include "pch.h"
#include "view_matrix_hook.h"
#include "camera_snapshot.h"
#include "core/seh_guard.h"
#include "core/vector_math.h"
#include "diagnostics/frame_verdict.h"
#include "game/fallout4_types.h"
#include "hook_slot.h"
#include "player_hook.h"
#include "core/logging.h"

namespace Fallout4HT {
namespace {

// Out-and-back detector over the last three samples of one quantity.
//
// A frame that renders from somewhere else and is then followed by one back
// where it was is a flicker, and it is visible here whatever caused it - a stale
// pose, a missed override, a race with the game's own update. It needs no screen
// capture, so CPU load, window position and capture timing cannot corrupt it,
// all of which have already produced false "clean" results.
//
// The sweep is smooth, so consecutive samples differ by a small amount. A
// genuine spike is: n-2 and n agree closely (within `acrossMax`), while n-1 sits
// far from both (beyond `awayMin`).
//
// Two of these run at once, over the rendered view DIRECTION and the camera's
// world POSITION, because neither can see the other's fault: a camera that jumps
// somewhere else while still looking the same way leaves the forward vector
// untouched, which is the exact shape of the user's screenshots.
struct OutAndBackDetector {
    // How the two samples' separation is measured - degrees between view
    // directions, or engine units between camera positions.
    float (*metric)(const float*, const float*);
    float acrossMax;
    float awayMin;

    std::atomic<uint64_t> count;
    float worst;
    float previous1[3];
    float previous2[3];
    int history;

    void Add(const float* sample) {
        if (history >= 2) {
            const float across = metric(previous2, sample);
            const float toMiddle = metric(previous1, sample);
            const float fromMiddle = metric(previous2, previous1);
            if (across < acrossMax && toMiddle > awayMin && fromMiddle > awayMin) {
                count.fetch_add(1, std::memory_order_relaxed);
                const float peak = toMiddle > fromMiddle ? toMiddle : fromMiddle;
                if (peak > worst) worst = peak;
            }
        }
        for (int i = 0; i < 3; ++i) {
            previous2[i] = previous1[i];
            previous1[i] = sample[i];
        }
        if (history < 2) ++history;
    }

    // Reading the count clears it, so the reporter's line covers exactly the
    // period since the last one.
    uint64_t TakeCount() { return count.exchange(0, std::memory_order_relaxed); }
    float TakeWorst() { const float w = worst; worst = 0.0f; return w; }
};

// mov rax,rsp / sub rsp,imm / movss xmm3,[rcx+0xA0] / movss xmm2,[rcx+0xA4].
// The frame size is masked - it is the byte most likely to move on a recompile
// that changes nothing else. The two float loads at +0xA0/+0xA4 are what make
// this unique.
const uint8_t kBuildViewPattern[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00,
    0xF3, 0x0F, 0x10, 0x99, 0xA0, 0x00, 0x00, 0x00,
    0xF3, 0x0F, 0x10, 0x91, 0xA4, 0x00, 0x00, 0x00
};

// Every argument is passed straight through: the signature is not known beyond
// the first pointer, and declaring fewer would let the compiler clobber the rest
// before the original ever sees them.
typedef void* (__fastcall *BuildViewMatrix_t)(void* a, void* b, void* c, void* d);
BuildViewMatrix_t g_originalBuild = nullptr;
HookSlot g_buildHook;

std::atomic<uint64_t> g_builds{0};
std::atomic<uint64_t> g_playerBuildsWithPose{0};

// Builds whose RESULT came out showing the stock camera - the defect itself,
// counted directly instead of inferred from how often it reaches the screen.
//
// Screen capture cannot resolve this: it lands 4-14 events per 2400-sample run,
// which is noise-dominated, and several changes were evaluated against that
// before anyone noticed. Builds happen ~1900 times a second, so a counter here
// gives thousands of samples a second and a difference that means something.
std::atomic<uint64_t> g_buildsWrong{0};

// Player builds that leave worldToCam byte-for-byte unchanged.
std::atomic<uint64_t> g_unchangedPlayerBuilds{0};

// The rendered view DIRECTION, in degrees.
OutAndBackDetector g_viewSpikes{ &DegreesBetweenUnit, 0.75f, 1.5f };

// The camera's WORLD POSITION, in engine units. Read from the node's world
// translation, NOT from the view matrix, whose translation column is -R*eye and
// therefore swings by hundreds of units on a fraction of a degree of rotation
// without the camera moving at all.
OutAndBackDetector g_posSpikes{ &Distance3, 2.0f, 8.0f };

// A single build's step in the rendered view. Well above a hand on the mouse
// between two consecutive builds at ~1900 builds a second, so anything past
// either is the camera being moved rather than aimed.
constexpr float kMatrixJumpDeg = 2.0f;
constexpr float kMatrixJumpUnits = 50.0f;

std::atomic<uint64_t> g_matrixJumps{0};
float g_previousForward[3] = {0.0f, 0.0f, 0.0f};
float g_previousTranslate[3] = {0.0f, 0.0f, 0.0f};
bool g_hasPreviousMatrix = false;
float g_worstJumpDeg = 0.0f;
float g_worstJumpUnits = 0.0f;

void NoteBuiltMatrix(const CameraRootSnapshots& snap) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const NiMatrix44* m = reinterpret_cast<const NiMatrix44*>(
            snap.niCamera + NiCameraOffsets::WorldToCam);
        float f[3] = { m->entry[2][0], m->entry[2][1], m->entry[2][2] };
        const float len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
        if (len <= 1e-6f) return;
        for (int i = 0; i < 3; ++i) f[i] /= len;
        const float t[3] = { m->entry[0][3], m->entry[1][3], m->entry[2][3] };

        if (g_hasPreviousMatrix) {
            const float deg = DegreesBetweenUnit(f, g_previousForward);
            const float units = Distance3(t, g_previousTranslate);
            if (deg > kMatrixJumpDeg || units > kMatrixJumpUnits) {
                g_matrixJumps.fetch_add(1, std::memory_order_relaxed);
                if (deg > g_worstJumpDeg) g_worstJumpDeg = deg;
                if (units > g_worstJumpUnits) g_worstJumpUnits = units;
            }
        }

        const NiPoint3* p = WorldTranslationOf(snap.niCamera);
        const float pos[3] = { p->x, p->y, p->z };
        g_posSpikes.Add(pos);
        g_viewSpikes.Add(f);

        for (int i = 0; i < 3; ++i) { g_previousForward[i] = f[i]; g_previousTranslate[i] = t[i]; }
        g_hasPreviousMatrix = true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "matrix jump", s_faults)) {
    }
}

// About 0.5 degrees apart. Tight enough that a head rotation cannot be mistaken
// for a match, loose enough to absorb the engine's own recompute noise.
constexpr float kSameCameraCosine = 0.99995f;

// Below this a row carries no usable direction and the comparison would be noise
// against noise.
constexpr float kMinLengthSquared = 1e-12f;

// Row 0 of niCamera's world rotation is the camera forward. Comparing the built
// matrix's own forward against the two candidates says what the RENDERER got,
// which is the only thing that matters.
bool BuiltMatrixMatchesLiveCamera(const CameraRootSnapshots& snap) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const NiMatrix44* matrix = reinterpret_cast<const NiMatrix44*>(
            snap.niCamera + NiCameraOffsets::WorldToCam);
        const NiMatrix33* camera = WorldRotationOf(snap.niCamera);
        float matrixLengthSquared = 0.0f;
        float cameraLengthSquared = 0.0f;
        float dot = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float matrixComponent = matrix->entry[2][i];
            const float cameraComponent = camera->entry[0][i];
            dot += matrixComponent * cameraComponent;
            matrixLengthSquared += matrixComponent * matrixComponent;
            cameraLengthSquared += cameraComponent * cameraComponent;
        }
        if (matrixLengthSquared <= kMinLengthSquared ||
            cameraLengthSquared <= kMinLengthSquared) {
            return false;
        }
        return dot / sqrtf(matrixLengthSquared * cameraLengthSquared) > kSameCameraCosine;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "built check", s_faults)) {
    }
    return false;
}

// One line, once, after the first second or so of player builds (two per frame
// at 120 fps), so a session that came up broken says so in the log without
// waiting for anyone to arm an instrument.
constexpr uint64_t kStartupValidationBuilds = 240;

// Does the build's first argument identify the camera it is for? If it does, the
// override can be limited to the player's own builds instead of firing on shadow
// and reflection passes that happen to read the same nodes.
std::atomic<uint64_t> g_argIsNiCamera{0};
std::atomic<uint64_t> g_argIsOther{0};
std::atomic<uint64_t> g_startupPlayerAttempts{0};
std::atomic<uint64_t> g_startupPlayerSuccesses{0};
std::atomic<uint64_t> g_startupWrongBuilds{0};

void* __fastcall BuildViewMatrixHook(void* a, void* b, void* c, void* d) {
    g_builds.fetch_add(1, std::memory_order_relaxed);

    // The first argument identifies the camera being built. Shadow and
    // reflection passes are left alone.
    {
        CameraRootSnapshots probe;
        if (GetCameraRootSnapshots(probe) && probe.niCamera != 0) {
            const bool isPlayer = reinterpret_cast<uintptr_t>(a) == probe.niCamera;
            RecordBuildExposure(RenderPoseIsHeld(), isPlayer);
            if (isPlayer) {
                g_argIsNiCamera.fetch_add(1, std::memory_order_relaxed);
                g_startupPlayerAttempts.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_argIsOther.fetch_add(1, std::memory_order_relaxed);
                return g_originalBuild(a, b, c, d);
            }
        }
    }

    // Nothing here writes the built matrix. It puts the head-tracked rotation
    // back into the camera nodes for the length of the build, so the engine's own
    // code derives a head-tracked view matrix from live node state. Writing a
    // matrix cached from the last camera tick would instead race the renderer
    // reading it and hand back a view one tick stale.
    CameraRootSnapshots snap;
    OverridePath path = OverridePath::None;
    OverrideReference reference{};
    if (!BeginTrackedOverride(snap, path, reference)) return g_originalBuild(a, b, c, d);

    g_playerBuildsWithPose.fetch_add(1, std::memory_order_relaxed);

    // What state the camera is in as the engine is about to read it. Recorded
    // before the build rather than after, so a build that recomputes the node
    // from its parent shows up as a difference between the two verdicts.
    RecordBuildVerdict(snap, path, reference);

    // What the player camera's matrix looked like going in, so the build can be
    // asked afterwards whether it was even about the player camera.
    float before[4][4] = {};
    bool haveBefore = false;
    {
        static std::atomic<uint64_t> s_faults{0};
        __try {
            std::memcpy(before, reinterpret_cast<const NiMatrix44*>(
                snap.niCamera + NiCameraOffsets::WorldToCam)->entry, sizeof(before));
            haveBefore = true;
        } __except (SehAbsorbAccessViolation(GetExceptionCode(), "before snap", s_faults)) {
        }
    }

    void* result = nullptr;
    __try {
        result = g_originalBuild(a, b, c, d);
    } __finally {
        if (haveBefore) {
            static std::atomic<uint64_t> s_faults{0};
            __try {
                const NiMatrix44* after = reinterpret_cast<const NiMatrix44*>(
                    snap.niCamera + NiCameraOffsets::WorldToCam);
                if (std::memcmp(before, after->entry, sizeof(before)) == 0) {
                    g_unchangedPlayerBuilds.fetch_add(1, std::memory_order_relaxed);
                }
            } __except (SehAbsorbAccessViolation(GetExceptionCode(), "after snap", s_faults)) {
            }
        }
        if (!BuiltMatrixMatchesLiveCamera(snap)) {
            g_buildsWrong.fetch_add(1, std::memory_order_relaxed);
            g_startupWrongBuilds.fetch_add(1, std::memory_order_relaxed);
        }
        RecordBuiltVerdict(snap, reference);
        NoteBuiltMatrix(snap);
        EndTrackedOverride(snap);
        const uint64_t successes =
            g_startupPlayerSuccesses.fetch_add(1, std::memory_order_relaxed) + 1;
        if (successes == kStartupValidationBuilds) {
            Log::Line("startup view validation: %llu attempts, %llu current-pose builds,"
                      " %llu wrong results",
                      static_cast<unsigned long long>(
                          g_startupPlayerAttempts.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(kStartupValidationBuilds),
                      static_cast<unsigned long long>(
                          g_startupWrongBuilds.load(std::memory_order_relaxed)));
        }
    }
    return result;
}

} // namespace

bool InstallViewMatrixHook(const TextSection& text, uintptr_t moduleBase) {
    const uintptr_t buildFn = FindUniquePattern(
        text, kBuildViewPattern, "xxxxxx????xxxxxxxxxxxxxxxx", "view matrix build");
    if (!buildFn) {
        Log::Line("WARN: view matrix build not found - the engine can rebuild a stock view matrix"
                  " while game logic is being shown the clean camera, and that one reaches the"
                  " screen");
        return false;
    }

    Log::Line("view matrix build found at RVA 0x%llX",
              static_cast<unsigned long long>(buildFn - moduleBase));

    if (!g_buildHook.Install(reinterpret_cast<void*>(buildFn),
                             reinterpret_cast<void*>(&BuildViewMatrixHook),
                             reinterpret_cast<void**>(&g_originalBuild), "view matrix build")) {
        return false;
    }
    Log::Line("view matrix hook installed - rebuilds run against the head-tracked camera");
    return true;
}

void ReportViewMatrixStats() {
    const OverrideSkips skips = TakeOverrideSkips();
    const unsigned long long stockSkips = static_cast<unsigned long long>(
        skips.noSnapshot + skips.noRenderPose + skips.rotateFailed);
    Log::Line("  view matrix: %llu builds, %llu player builds received the current pose,"
              " %llu serialized overlaps, %llu BUILT WRONG | %llu matrix jumps"
              " (worst %.1f deg, %.0f units)"
               " | %llu unchanged player builds | arg==niCamera %llu, other %llu",
              static_cast<unsigned long long>(g_builds.exchange(0, std::memory_order_relaxed)),
              static_cast<unsigned long long>(
                   g_playerBuildsWithPose.exchange(0, std::memory_order_relaxed)),
              static_cast<unsigned long long>(TakeOverrideOverlaps()),
              static_cast<unsigned long long>(
                   g_buildsWrong.exchange(0, std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_matrixJumps.exchange(0, std::memory_order_relaxed)),
              g_worstJumpDeg, g_worstJumpUnits,
              static_cast<unsigned long long>(
                   g_unchangedPlayerBuilds.exchange(0, std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_argIsNiCamera.exchange(0, std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_argIsOther.exchange(0, std::memory_order_relaxed)));
    Log::Line("  POSITION SPIKES (camera jumped somewhere and back): %llu (worst %.0f units)",
              static_cast<unsigned long long>(g_posSpikes.TakeCount()),
              g_posSpikes.TakeWorst());
    Log::Line("  VIEW SPIKES (frame rendered from a different camera and back): %llu"
              " (worst %.1f deg)",
              static_cast<unsigned long long>(g_viewSpikes.TakeCount()),
              g_viewSpikes.TakeWorst());
    Log::Line("  player builds without render pose: %llu = %llu no snapshot +"
              " %llu no render pose + %llu apply failed",
              stockSkips,
              static_cast<unsigned long long>(skips.noSnapshot),
              static_cast<unsigned long long>(skips.noRenderPose),
              static_cast<unsigned long long>(skips.rotateFailed));
    g_worstJumpDeg = 0.0f;
    g_worstJumpUnits = 0.0f;
}

void RemoveViewMatrixHook() {
    g_buildHook.Remove();
}

} // namespace Fallout4HT
