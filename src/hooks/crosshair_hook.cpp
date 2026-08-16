// SPDX-License-Identifier: MIT

#include "pch.h"
#include "crosshair_hook.h"
#include "diagnostics/ab_switches.h"
#include "camera_snapshot.h"
#include "crosshair_layout.h"
#include "hook_slot.h"
#include "core/logging.h"
#include "core/seh_guard.h"

namespace Fallout4HT {
namespace {

// ---------------------------------------------------------------------------
// Moving the reticle goes through Scaleform's DisplayInfo, which is how the game
// itself changes the crosshair (it uses the same scope object to set alpha).
// Setting the "_x" member instead does nothing: the game's own SetMember is only
// used for custom ActionScript properties like "requestedRadius", and a forced
// +-220px sweep through it left the crosshair provably motionless.
//
// Scope layout, read off the game's alpha path:
//   +0x000  target object pointer
//   +0x010  CURRENT DisplayInfo, filled by the init call (GetDisplayInfo)
//   +0x0F0  PENDING DisplayInfo   X @+0x00, Y @+0x08, ... Alpha @+0x28
//   +0x1C4  pending dirty flags   X=0x01 Y=0x02 Alpha=0x20 Visible=0x40
// Alpha at +0x28 with flag 0x20 matches Scaleform's documented
// {X,Y,Rotation,XScale,YScale,Alpha} ordering, which is what pins X/Y.
// ---------------------------------------------------------------------------
typedef void* (__fastcall *GfxScopeInit_t)(void* scope, void* gfxObject);
typedef void  (__fastcall *GfxScopeApply_t)(void* scope);
typedef void  (__fastcall *HUDCrosshairUpdate_t)(void* thisCrosshair);

GfxScopeInit_t g_gfxScopeInit = nullptr;
GfxScopeApply_t g_gfxScopeApply = nullptr;
HUDCrosshairUpdate_t g_originalCrosshairUpdate = nullptr;
HookSlot g_crosshairHook;

// The FXCrosshairBase wrapper for CrosshairBase_mc, cached by the HUDCrosshair
// constructor. Its first field is a vtable pointer and the GFxValue proper
// begins at +0x100; the scope helper takes the wrapper.
constexpr uintptr_t kHUDCrosshair_BaseClip = 0xF8;
constexpr uintptr_t kScopeCurrent = 0x10;
constexpr uintptr_t kScopePending = 0xF0;
constexpr uintptr_t kScopeFlags = 0x1C4;
constexpr uint16_t kDisplayInfoX = 0x01;
constexpr uint16_t kDisplayInfoY = 0x02;

// Indices into a DisplayInfo, whose layout is {X, Y, Rotation, XScale, YScale,
// Alpha} as doubles.
constexpr int kDisplayInfoIndexX = 0;
constexpr int kDisplayInfoIndexY = 1;
constexpr int kDisplayInfoIndexXScale = 3;
constexpr int kDisplayInfoIndexYScale = 4;

// The game's own callers give this scope a 0x1D0-byte stack local (the
// decompiler renders it `undefined1 local_228 [464]`). This is that with room to
// spare, and zeroing it means the apply call sees no dirty flag we did not set
// ourselves.
constexpr size_t kScopeBufferSize = 0x260;

double g_clipBaseX = 0.0;
double g_clipBaseY = 0.0;
bool g_clipBaseValid = false;

// Lets the untracked path leave the HUD alone rather than writing the same zero
// offset through Scaleform on every crosshair tick for the whole session.
bool g_crosshairMoved = false;

// Read the clip's authored position out of an initialised scope, and stage the
// offset we want. Only our own accesses to the scope buffer are guarded; the two
// Scaleform calls that bracket this sit outside, where a fault of theirs belongs
// to the game. Returns false if the scope could not be read.
bool StageCrosshairOffset(uint8_t* scope, double dx, double dy,
                          std::atomic<uint64_t>& faults) {
    __try {
        const double* current = reinterpret_cast<const double*>(scope + kScopeCurrent);

        // Refreshed only while the crosshair is sitting at its authored spot.
        // Re-reading unconditionally is wrong - the position the init call
        // reports already includes whatever we wrote last frame, so offsets
        // compound and a zero offset "restores" to wherever tracking last left
        // it. Refreshing on the untouched frames instead keeps that from
        // happening while still recovering if the HUD movie is rebuilt.
        //
        // A zero scale means the init call found no live clip and left the
        // buffer as we zeroed it; latching that as the base would peg the
        // reticle to the stage origin for the rest of the session.
        const double xScale = current[kDisplayInfoIndexXScale];
        const double yScale = current[kDisplayInfoIndexYScale];
        if (!g_crosshairMoved && xScale != 0.0 && yScale != 0.0) {
            g_clipBaseX = current[kDisplayInfoIndexX];
            g_clipBaseY = current[kDisplayInfoIndexY];
            if (!g_clipBaseValid) {
                g_clipBaseValid = true;
                // Scale is logged too: HUD replacements such as DEF_UI reparent
                // the crosshair, and a parent scale other than 100 silently
                // multiplies every offset below.
                Log::Line("crosshair: authored position (%.2f, %.2f) scale (%.1f, %.1f)",
                          g_clipBaseX, g_clipBaseY, xScale, yScale);
            }
        }
        if (!g_clipBaseValid) return false;

        double* pending = reinterpret_cast<double*>(scope + kScopePending);
        pending[kDisplayInfoIndexX] = g_clipBaseX + dx;
        pending[kDisplayInfoIndexY] = g_clipBaseY + dy;
        *reinterpret_cast<uint16_t*>(scope + kScopeFlags) |= (kDisplayInfoX | kDisplayInfoY);
        return true;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "crosshair scope", faults)) {
    }
    return false;
}

bool MoveCrosshair(void* crosshair, double dx, double dy, std::atomic<uint64_t>& faults) {
    void* target = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(crosshair) + kHUDCrosshair_BaseClip);

    alignas(16) uint8_t scope[kScopeBufferSize];
    std::memset(scope, 0, sizeof(scope));
    g_gfxScopeInit(scope, target);

    if (!StageCrosshairOffset(scope, dx, dy, faults)) return false;
    g_gfxScopeApply(scope);
    return true;
}

void __fastcall HUDCrosshairUpdateHook(void* thisCrosshair) {
    g_originalCrosshairUpdate(thisCrosshair);

    // Zeroed up front: a reader that finds nothing published leaves `snap`
    // untouched, and the aim fields are read unconditionally below.
    CameraRootSnapshots snap{};
    const bool haveSnap = GetCameraRootSnapshots(snap);

    const CrosshairStageOffset offset = AbSwitches::CrosshairMoveEnabled()
        ? ComputeCrosshairStageOffset(haveSnap, snap.aimValid, snap.aimNdcX, snap.aimNdcY,
                                      snap.frustumRight, snap.frustumTop)
        : CrosshairStageOffset{};

    const bool wantMoved = (offset.dx != 0.0 || offset.dy != 0.0);
    if (!wantMoved && !g_crosshairMoved) return;

    // Only latch the new state if the write landed, so a failed move still
    // leaves the "needs restoring" flag set.
    static std::atomic<uint64_t> s_faults{0};
    if (MoveCrosshair(thisCrosshair, offset.dx, offset.dy, s_faults)) {
        g_crosshairMoved = wantMoved;
    }
}

// sub rsp,imm is masked like the others. The +0x10 is NOT a frame size - it is
// the offset of the scope's DisplayInfo, and dropping it costs uniqueness (two
// matches), so it stays required. Trailing E8 is a call whose displacement is
// masked.
const uint8_t kScopeInitPattern[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x00, 0x48,
    0x8B, 0xF9, 0x48, 0x89, 0x11, 0x48, 0x83, 0xC1, 0x10, 0xE8, 0x00,
    0x00, 0x00, 0x00
};
// movzx eax,[rcx+0x1C4] - that displacement is the dirty-flags offset the hook
// itself writes, so requiring it doubles as a check that the offset still holds.
// The 0FFFh mask immediate is masked off.
const uint8_t kScopeApplyPattern[] = {
    0x0F, 0xB7, 0x81, 0xC4, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xC1, 0xB9,
    0x00, 0x00, 0x00, 0x00, 0x66, 0x23, 0xC1
};
// movzx edx, byte ptr [rip+disp32] - displacement masked, and so is the frame
// size. Ends on a whole `mov rdi,rcx` rather than a bare REX+opcode.
const uint8_t kCrosshairUpdatePattern[] = {
    0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0xB6, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9
};

} // namespace

void InstallCrosshairHook(const TextSection& text, uintptr_t moduleBase) {
    const uintptr_t scopeInitFn = FindUniquePattern(
        text, kScopeInitPattern, "xxxxxxxxx?xxxxxxxxxxx????", "scope init");
    const uintptr_t scopeApplyFn = FindUniquePattern(
        text, kScopeApplyPattern, "xxxxxxxxxxx????xxx", "scope apply");
    const uintptr_t crosshairFn = FindUniquePattern(
        text, kCrosshairUpdatePattern, "xxxxxxx????xxx????xxx", "crosshair update");

    if (!scopeInitFn || !scopeApplyFn || !crosshairFn) {
        Log::Line("WARN: crosshair plumbing not found (init=%d apply=%d update=%d)"
                  " - shots follow body aim but the reticle stays at screen centre",
                  scopeInitFn ? 1 : 0, scopeApplyFn ? 1 : 0, crosshairFn ? 1 : 0);
        return;
    }

    Log::Line("crosshair: scope init RVA 0x%llX apply RVA 0x%llX update RVA 0x%llX",
              static_cast<unsigned long long>(scopeInitFn - moduleBase),
              static_cast<unsigned long long>(scopeApplyFn - moduleBase),
              static_cast<unsigned long long>(crosshairFn - moduleBase));

    if (!g_crosshairHook.Install(reinterpret_cast<void*>(crosshairFn),
                                 reinterpret_cast<void*>(&HUDCrosshairUpdateHook),
                                 reinterpret_cast<void**>(&g_originalCrosshairUpdate),
                                 "HUDCrosshair::Update")) {
        return;
    }

    // Only published once the hook that uses them is live, so a failed install
    // cannot leave the scope helpers armed with no caller.
    g_gfxScopeInit = reinterpret_cast<GfxScopeInit_t>(scopeInitFn);
    g_gfxScopeApply = reinterpret_cast<GfxScopeApply_t>(scopeApplyFn);
    Log::Line("crosshair hook installed");
}

void RemoveCrosshairHook() {
    if (!g_crosshairHook.IsInstalled()) return;
    g_crosshairHook.Remove();
    g_gfxScopeInit = nullptr;
    g_gfxScopeApply = nullptr;
}

} // namespace Fallout4HT
