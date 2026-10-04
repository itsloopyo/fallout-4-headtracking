// SPDX-License-Identifier: MIT

#include "pch.h"
#include "aim_decoupling.h"
#include "hook_slot.h"
#include "player_hook.h"
#include "activation_hook.h"
#include "camera_math.h"
#include "module_scan.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "core/vector_math.h"
#include "camera_snapshot.h"
#include "game/fallout4_types.h"

#include <cmath>

namespace Fallout4HT {
namespace {

// Runs a call with niCamera showing the body's rotation instead of the head's.
//
// Used by the fire path only. A shot needs BOTH things body-aimed to land on the
// reticle - the camera the launch reads, and the auto-aim point it converges
// onto - so the scope also re-solves auto-aim while it is open. Measured with a
// 35 degree head turn: camera alone still swung the shot 46.5 degrees, camera
// plus the re-solve swings it 2.6.
//
// __finally rather than a destructor because what unwinds out of the game's own
// functions is SEH, and leaving this scope early would leave the camera swapped
// for the rest of the session with the mutex still held.
template <typename Fn>
auto WithCleanCamera(Fn call) -> decltype(call()) {
    BeginAimCleanScope();
    __try {
        return call();
    } __finally {
        EndAimCleanScope();
    }
}

// ---------------------------------------------------------------------------
// THE FIRE PATH.
//
// The projectile launch data is filled by one function, which takes its
// direction from the camera node reached through PlayerCamera+0x20, reading
// row1 (+0x80) - the forward axis. So a shot goes wherever that node points,
// which is why head tracking dragged the bullets to screen centre.
//
// That node is NICAMERA, the one the held head pose is applied to - not
// cameraRoot, which this comment claimed for a while and which made it look
// safe to stop stripping the pose here. Logged at the instant the launch is
// built, with the head 20 degrees off the body: cameraRoot 0.02 deg from the
// body, niCamera 0.03 deg from the HEAD. Hence BeginAimCleanScope, which always
// takes the pose off, rather than the player update's scope, which must not.
//
// Established two independent ways: the shipped mod AutoBeam takes the
// first-person bullet direction from this same matrix, and a decompile of this
// function shows the same read feeding the launch angles.
//
// This function carries no RTTI; each build's pattern must be unique in .text.
// ---------------------------------------------------------------------------
// Return type is the full register rather than bool: a decompile shows the
// function only ever sets AL and leaves the rest of RAX as it found it, so
// passing the value through whole cannot differ from what the game returns.
typedef uintptr_t (__fastcall *ComputeLaunchData_t)(void* launchData);
ComputeLaunchData_t g_originalLaunch = nullptr;
HookSlot g_launchHook;

// Where a shot actually went, in the only terms that settle the question: the
// launch yaw the game just wrote, next to the yaw of the body's camera and the
// yaw of the head-tracked one. A screenshot of an impact cannot separate those
// two when they are a few degrees apart; this can, on every shot.
constexpr uintptr_t kLaunchAngleZ = 0x4C;   // yaw, radians
constexpr uintptr_t kLaunchAngleX = 0x50;   // pitch, radians

float YawOfForward(const float row1[4]) {
    return atan2f(row1[0], row1[1]) / DEG_TO_RAD;
}

float PitchOfForward(const float row1[4]) {
    return asinf(ClampToUnitRange(row1[2])) / DEG_TO_RAD;
}

float SignedDelta(float a, float b) {
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// One SHOT line and one BAD SHOT line a second at most. An automatic weapon
// would otherwise fill the log faster than it can be read, and the question
// these lines answer - does the launch follow the body or the head - is settled
// by any one of them.
constexpr uint64_t kLaunchReportIntervalMs = 1000;

bool LaunchReportDue(std::atomic<uint64_t>& lastReportMs) {
    const uint64_t nowMs = GetTickCount64();
    if (nowMs - lastReportMs.load(std::memory_order_relaxed) < kLaunchReportIntervalMs) {
        return false;
    }
    lastReportMs.store(nowMs, std::memory_order_relaxed);
    return true;
}

void ReportLaunch(void* launchData) {
    static std::atomic<uint64_t> s_lastReportMs{0};

    CameraRootSnapshots snap{};
    if (!GetCameraRootSnapshots(snap) || snap.cameraRoot == 0) return;
    // Gated after the snapshot read so a frame without snapshots does not consume
    // the interval the next reportable shot needs.
    if (!LaunchReportDue(s_lastReportMs)) return;

    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uintptr_t base = reinterpret_cast<uintptr_t>(launchData);

        const float launchYaw =
            *reinterpret_cast<const float*>(base + kLaunchAngleZ) / DEG_TO_RAD;
        const float launchPitch =
            *reinterpret_cast<const float*>(base + kLaunchAngleX) / DEG_TO_RAD;

        const float bodyYaw = YawOfForward(snap.cleanWorld[1]);
        const float headYaw = YawOfForward(snap.trackedWorld[1]);
        const float toBody = SignedDelta(launchYaw, bodyYaw);
        const float toHead = SignedDelta(launchYaw, headYaw);

        const float bodyPitch = PitchOfForward(snap.cleanWorld[1]);
        const float headPitch = PitchOfForward(snap.trackedWorld[1]);
        Log::Line("SHOT: launch yaw %+.2f pitch %+.2f | body yaw %+.2f pitch %+.2f"
                  " (off by %+.2f) | head yaw %+.2f pitch %+.2f (off by %+.2f)"
                  " | head is %+.2f deg from body -> %s",
                  launchYaw, launchPitch, bodyYaw, bodyPitch,
                  toBody, headYaw, headPitch, toHead,
                  SignedDelta(headYaw, bodyYaw),
                  fabsf(toBody) <= fabsf(toHead) ? "FOLLOWS THE BODY (decoupled)"
                                                 : "FOLLOWS THE HEAD (coupled)");

    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "launch report", s_faults)) {
    }
}

// Divides by DEG_TO_RAD rather than multiplying by RAD_TO_DEG - see constants.h.
float AngleBetween(const float a[4], const float b[4]) {
    return RadiansBetweenUnit(a, b) / DEG_TO_RAD;
}

// What the two camera nodes look like at the instant the game reads them to
// build a shot. This is the question the whole fire path turns on, and it is
// answerable without knowing one field of the projectile struct: if a node still
// carries the head rotation here, every shot taken from it follows the head.
void ReportCameraSeenByLaunch() {
    CameraRootSnapshots snap{};
    if (!GetCameraRootSnapshots(snap) || snap.cameraRoot == 0 || snap.niCamera == 0) return;

    static std::atomic<uint64_t> s_faults{0};
    static std::atomic<uint64_t> s_lastBadReportMs{0};
    __try {
        const NiMatrix33* rootLive = WorldRotationOf(snap.cameraRoot);
        const NiMatrix33* camLive = WorldRotationOf(snap.niCamera);
        const float headOffset = AngleBetween(snap.cleanWorld[1], snap.trackedWorld[1]);
        const float rootToClean = AngleBetween(rootLive->entry[1], snap.cleanWorld[1]);
        const float rootToTracked = AngleBetween(rootLive->entry[1], snap.trackedWorld[1]);
        const float camToClean = AngleBetween(camLive->entry[1], snap.cleanNiCamWorld[1]);
        const float camToTracked = AngleBetween(camLive->entry[1], snap.trackedNiCamWorld[1]);

        const bool rootFollowsHead = rootToTracked < rootToClean;
        const bool camFollowsHead = camToTracked < camToClean;
        if (!rootFollowsHead && !camFollowsHead) return;   // both body-aimed, nothing to say
        // Gated here rather than on entry so a body-aimed shot does not consume
        // the interval that the next bad one needs.
        if (!LaunchReportDue(s_lastBadReportMs)) return;
        Log::Line("BAD SHOT: the camera the launch reads still carries the head pose -"
                  " cameraRoot %s (%.2f deg from body, %.2f from head), niCamera %s"
                  " (%.2f from body, %.2f from head), head is %.2f deg off. This shot"
                  " goes where the player is LOOKING, not where the reticle is.",
                  rootFollowsHead ? "HEAD-TRACKED" : "body-aimed", rootToClean, rootToTracked,
                  camFollowsHead ? "HEAD-TRACKED" : "body-aimed", camToClean, camToTracked,
                  headOffset);
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "fire path camera", s_faults)) {
    }
}

// Recompute the player's auto-aim point from the camera as it is right now -
// which, inside the fire path's clean scope, is the body's.
//
// The launch converges onto this cached point in preference to the camera
// forward, so leaving it as the per-frame solve left it is what made shots
// follow the head even with a clean camera under them: measured, a 35 degree
// head turn moved the launch 46.5 degrees. Re-solving here moves it 1.7.
void RefreshAutoAimFromBody();   // defined with the solver it re-runs

// The actor whose shot this is. Every actor in the game goes through this
// function, and until this was pinned an NPC firing across the street was
// measured as the player's shot - which flipped the coupled/decoupled verdict
// run to run depending on whether a gunfight happened to be in earshot. The
// call does NOT happen inside PlayerCharacter::Update (measured: 0 of 15
// calls), so the struct is the only place the answer lives. Found by scanning
// the struct for the player's own pointer.
constexpr uintptr_t kLaunchShooter = 0x20;

bool LaunchIsPlayers(void* launchData) {
    static std::atomic<uint64_t> s_faults{0};
    __try {
        return IsPlayerActor(*reinterpret_cast<void**>(
            reinterpret_cast<uintptr_t>(launchData) + kLaunchShooter));
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "launch shooter", s_faults)) {
    }
    return false;
}

uintptr_t __fastcall ComputeLaunchDataHook(void* launchData) {
    if (!LaunchIsPlayers(launchData)) return g_originalLaunch(launchData);

    const uintptr_t result = WithCleanCamera([launchData] {
        ReportCameraSeenByLaunch();
        RefreshAutoAimFromBody();
        return g_originalLaunch(launchData);
    });
    ReportLaunch(launchData);
    return result;
}

// The frame-size immediates in this prologue are masked off. They are the bytes
// most likely to move on a recompile that changes nothing else, and masking them
// costs no uniqueness: the pattern was confirmed to still match exactly one site
// in .text without them.
//
// mov r11,rsp / push rbp / push rdi / lea rbp,[r11-disp] / sub rsp,imm /
// mov rdi,rcx / xor al,al / mov rcx,[rcx+20h] / test rcx,rcx.
//
// The pattern runs past the prologue on purpose. Two other functions share every
// prologue byte of this one and differ only at `mov rdi,rcx`, so stopping there
// would leave a mandatory hook resting on a single ModRM byte - anything that
// moved `this` to another register would silently drop it to no match at all.
const uint8_t kLaunchPattern[] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x57, 0x49, 0x8D, 0xAB, 0x00, 0x00, 0x00,
    0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9,
    0x32, 0xC0, 0x48, 0x8B, 0x49, 0x20, 0x48, 0x85, 0xC9
};

// The same function on 1.10.163, which is what GOG sells and has sold since
// 2023 - it never took the next-gen update. Same body, differently scheduled:
// the `xor al,al` above sits between `mov rdi,rcx` and the camera fetch on the
// newer build and simply is not there on this one, which is enough to sink the
// pattern above. The prologue differs too (register saves instead of the
// `mov r11,rsp` frame). Confirmed by decompiling 1.10.163's own binary: it takes
// its direction from row1 at +0x80 of the node behind arg+0x20 and writes the
// launch angles to +0x4C / +0x50, exactly as the newer build does.
//
// Both halves are load-bearing here as well - the prologue alone matches 17
// sites in .text and the tail alone matches 17.
const uint8_t kLaunchPattern110163[] = {
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00,
    0x00, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x49, 0x20, 0x48, 0x85, 0xC9
};

const uint8_t kLaunchPatternXbox[] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x57, 0x41, 0x54, 0x49, 0x8D, 0x6B, 0x00,
    0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0x32,
    0xC0, 0x48, 0x8B, 0x49, 0x20, 0x48, 0x85, 0xC9
};

// ---------------------------------------------------------------------------
// THE AUTO-AIM SOLVER.
//
// The launch function converges onto a cached auto-aim point at
// PlayerCharacter+0x720 and uses it in preference to the camera forward, so
// feeding the launch a clean camera is not enough on its own - the cached point
// is recomputed every frame from the head-tracked camera by this solver.
// Sandwiching the solver too makes auto-aim target what the BODY points at.
// ---------------------------------------------------------------------------
// Signature carries a float in xmm1, so it is declared in full: an omitted float
// argument is not merely ignored, the compiler is then free to clobber xmm1 in
// our body before the original ever sees it.
typedef void* (__fastcall *AutoAimSolver_t)(void* actor, float maxDist, char sighted);
AutoAimSolver_t g_originalAutoAim = nullptr;
HookSlot g_autoAimHook;

// The arguments the game last solved the player's auto-aim with, so the same
// solve can be repeated at the moment of firing.
std::atomic<float> g_autoAimMaxDist{0.0f};
std::atomic<char> g_autoAimSighted{0};
std::atomic<bool> g_autoAimSeen{false};

// ---------------------------------------------------------------------------
// WHO IS UNDER THE CROSSHAIR.
//
// The solver picks its actor on screen: it projects each candidate's bound
// through the camera and keeps the one nearest the centre. That actor is what
// the HUD names and what an aimed weapon can act on (the sights-up prompt on a
// character). Through the head-tracked camera it is whoever the view centre is
// on, so the prompt lit for the view and stayed dark for the sights.
//
// The projection takes the camera as an argument and the solver is its only
// caller, so the player's solve is handed a private copy of the camera standing
// where the body aims. Nothing the renderer reads is written.
// ---------------------------------------------------------------------------
typedef uintptr_t (__fastcall *BoundToScreen_t)(void* camera, const float* bound, float* nearPoint, float* farPoint,
                                                float tolerance);
BoundToScreen_t g_originalBoundToScreen = nullptr;
HookSlot g_boundToScreenHook;
thread_local bool t_solvingForPlayer = false;

// The projection reads the camera's world rotation and translation, worldToCam
// and the frustum, the last of them at +0x178.
constexpr size_t kCameraBytesRead = 0x180;

bool CleanCameraCopy(void* camera, uint8_t* copy) {
    CameraRootSnapshots snap{};
    AimReference reference;
    if (!GetCameraRootSnapshots(snap) || snap.niCamera != reinterpret_cast<uintptr_t>(camera) ||
        !GetAimReference(reference)) {
        return false;
    }
    static std::atomic<uint64_t> s_faults{0};
    __try {
        std::memcpy(copy, camera, kCameraBytesRead);
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "crosshair target camera", s_faults)) {
        return false;
    }
    const uintptr_t node = reinterpret_cast<uintptr_t>(copy);
    NiMatrix33 clean;
    std::memcpy(clean.entry, snap.cleanNiCamWorld, sizeof(clean.entry));
    NiMatrix44* worldToCam = reinterpret_cast<NiMatrix44*>(node + NiCameraOffsets::WorldToCam);
    *worldToCam = RebaseWorldToCam(*worldToCam, *WorldRotationOf(node), *WorldTranslationOf(node), clean,
                                   reference.cleanEye);
    *WorldRotationOf(node) = clean;
    *WorldTranslationOf(node) = reference.cleanEye;
    return true;
}

uintptr_t __fastcall BoundToScreenHook(void* camera, const float* bound, float* nearPoint, float* farPoint,
                                       float tolerance) {
    alignas(16) uint8_t copy[kCameraBytesRead];
    if (t_solvingForPlayer && CleanCameraCopy(camera, copy)) {
        return g_originalBoundToScreen(copy, bound, nearPoint, farPoint, tolerance);
    }
    return g_originalBoundToScreen(camera, bound, nearPoint, farPoint, tolerance);
}

void* __fastcall AutoAimSolverHook(void* actor, float maxDist, char sighted) {
    const bool player = IsPlayerActor(actor);
    if (player) {
        g_autoAimMaxDist.store(maxDist, std::memory_order_relaxed);
        g_autoAimSighted.store(sighted, std::memory_order_relaxed);
        g_autoAimSeen.store(true, std::memory_order_relaxed);
    }
    // Deliberately NOT wrapped. This runs every frame, and even the cheap swap
    // leaves niCamera body-aimed for the length of the solve - 16.7 us measured,
    // 120 times a second - which is render-thread exposure of exactly the kind
    // the flicker is made of. The cache it writes is instead recomputed from the
    // body's camera at the moment of firing, where the window costs nothing
    // because it happens once per shot.
    const bool outer = t_solvingForPlayer;
    t_solvingForPlayer = player;
    void* result = g_originalAutoAim(actor, maxDist, sighted);
    t_solvingForPlayer = outer;
    return result;
}

void RefreshAutoAimFromBody() {
    if (!g_autoAimSeen.load(std::memory_order_relaxed)) return;
    void* player = PlayerActor();
    if (player == nullptr) return;
    // The camera itself is clean here, so the projection takes it as it is.
    g_originalAutoAim(player, g_autoAimMaxDist.load(std::memory_order_relaxed),
                      g_autoAimSighted.load(std::memory_order_relaxed));
}

const uint8_t kAutoAimPattern[] = {
    0x40, 0x55, 0x53, 0x56, 0x41, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0x00,
    0x00, 0x00, 0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0x01
};

const uint8_t kAutoAimPatternXbox[] = {
    0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x33, 0xFF,
    0x44, 0x0F, 0x29, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00, 0x89, 0x7C,
    0x24, 0x00, 0x44, 0x0F, 0x28, 0xE1, 0x48, 0x8B, 0x01, 0x48, 0x8B,
    0xF1, 0x41, 0x0F, 0xB6, 0xD8
};

} // namespace

bool InstallFirePathHook(const TextSection& text, uintptr_t moduleBase) {
    const char* variant = "none";
    uintptr_t launchFn = FindUniquePatternEither(
        text, kLaunchPattern, "xxxxxxxx????xxx????xxxxxxxxxxxx", "1.11",
        kLaunchPattern110163, "xxxxxxxxxxxxxxx?xxx????xxxxxxxxxx", "1.10.163",
        "fire path", variant);
    if (!launchFn) {
        launchFn = FindUniquePattern(text, kLaunchPatternXbox,
                                     "xxxxxxxxxx?xxx????xxxxxxxxxxxx", "fire path Xbox");
        if (launchFn) variant = "Xbox 1.11.240";
    }
    if (!launchFn) {
        Log::Line("ERROR: fire path not found - aim decoupling is unavailable on this build,"
                  " so head tracking is staying off rather than aiming the reticle away from"
                  " where shots go");
        return false;
    }
    Log::Line("fire path found at RVA 0x%llX (%s prologue)",
              static_cast<unsigned long long>(launchFn - moduleBase), variant);

    if (!g_launchHook.Install(reinterpret_cast<void*>(launchFn),
                              reinterpret_cast<void*>(&ComputeLaunchDataHook),
                              reinterpret_cast<void**>(&g_originalLaunch), "fire path")) {
        return false;
    }
    Log::Line("fire path hook installed - shots follow body aim");
    return true;
}

// How far into the solver the two projection calls sit, and how far before a
// call its camera argument is loaded.
constexpr size_t kSolverScanBytes = 0x800;
constexpr size_t kArgumentWindowBytes = 0x40;
constexpr int kMaxCallCandidates = 64;
// mov rcx, r13: the solver keeps the camera in r13 on every build seen.
const uint8_t kCameraIntoRcx[] = {0x49, 0x8B, 0xCD};

bool LoadsCameraBefore(uintptr_t call) {
    for (size_t back = sizeof(kCameraIntoRcx); back <= kArgumentWindowBytes; ++back) {
        if (std::memcmp(reinterpret_cast<const void*>(call - back), kCameraIntoRcx, sizeof(kCameraIntoRcx)) == 0) {
            return true;
        }
    }
    return false;
}

// The projection the solver calls: the one function it calls directly exactly
// twice, each time with the camera as the first argument. 0 unless exactly one
// function fits.
uintptr_t FindBoundToScreen(const TextSection& text, uintptr_t solver) {
    struct Candidate {
        uintptr_t target;
        int calls;
        int withCamera;
    };
    Candidate candidates[kMaxCallCandidates];
    int count = 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        for (size_t off = kArgumentWindowBytes; off + 5 <= kSolverScanBytes; ++off) {
            const uintptr_t at = solver + off;
            if (*reinterpret_cast<const uint8_t*>(at) != 0xE8) continue;
            const uintptr_t target = at + 5 + *reinterpret_cast<const int32_t*>(at + 1);
            if (target < text.start || target >= text.start + text.size) continue;
            int index = 0;
            while (index < count && candidates[index].target != target) ++index;
            if (index == count) {
                if (count == kMaxCallCandidates) return 0;
                candidates[count++] = Candidate{target, 0, 0};
            }
            ++candidates[index].calls;
            if (LoadsCameraBefore(at)) ++candidates[index].withCamera;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "crosshair target scan", s_faults)) {
        return 0;
    }
    uintptr_t found = 0;
    int fits = 0;
    for (int i = 0; i < count; ++i) {
        if (candidates[i].calls != 2 || candidates[i].withCamera != 2) continue;
        found = candidates[i].target;
        ++fits;
    }
    return fits == 1 ? found : 0;
}

// Optional: without it shots still follow the body's aim, and the character the
// HUD names with the sights up is the one at the centre of the view.
void InstallCrosshairTargetHook(const TextSection& text, uintptr_t moduleBase, uintptr_t solver) {
    const uintptr_t projection = FindBoundToScreen(text, solver);
    if (projection == 0) {
        Log::Line("ERROR: crosshair target: the solver's screen projection was not found on this build - the"
                  " character named under the crosshair follows the view, not the aim");
        return;
    }
    Log::Line("crosshair target: screen projection at RVA 0x%llX",
              static_cast<unsigned long long>(projection - moduleBase));
    if (g_boundToScreenHook.Install(reinterpret_cast<void*>(projection), reinterpret_cast<void*>(&BoundToScreenHook),
                                    reinterpret_cast<void**>(&g_originalBoundToScreen), "crosshair target")) {
        Log::Line("crosshair target hook installed - the character under the crosshair follows the aim");
    }
}

bool InstallAutoAimHook(const TextSection& text, uintptr_t moduleBase) {
    const char* variant = "none";
    const uintptr_t autoAimFn = FindUniquePatternEither(
        text, kAutoAimPattern, "xxxxxxxxxx????xxx????xxx", "Steam/GOG",
        kAutoAimPatternXbox, "xxxxxxxxx????xxx????xxxxxxx????xxx?xxxxxxxxxxxxxx", "Xbox 1.11.240",
        "auto-aim solver", variant);
    if (!autoAimFn) {
        Log::Line("ERROR: auto-aim solver not found - aim decoupling is incomplete");
        return false;
    }

    Log::Line("auto-aim solver found at RVA 0x%llX (%s)",
              static_cast<unsigned long long>(autoAimFn - moduleBase), variant);
    // Before the solver's own hook goes on, while its bytes are the game's.
    InstallCrosshairTargetHook(text, moduleBase, autoAimFn);
    if (g_autoAimHook.Install(reinterpret_cast<void*>(autoAimFn),
                              reinterpret_cast<void*>(&AutoAimSolverHook),
                              reinterpret_cast<void**>(&g_originalAutoAim), "auto-aim solver")) {
        Log::Line("auto-aim solver hook installed - aim point follows body");
        return true;
    }
    return false;
}

void RemoveAimDecouplingHooks() {
    g_boundToScreenHook.Remove();
    g_autoAimHook.Remove();
    g_launchHook.Remove();
}

} // namespace Fallout4HT
