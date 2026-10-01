// SPDX-License-Identifier: MIT

#include "pch.h"
#include "hit_marker_hook.h"
#include "camera_math.h"
#include "crosshair_layout.h"
#include "hook_slot.h"
#include "module_scan.h"
#include "core/logging.h"
#include "core/seh_guard.h"
#include "ui/game_window.h"

#include <cameraunlock/memory/rtti_vtable.h>

namespace Fallout4HT {
namespace {

using HitEvent = uint32_t(__fastcall*)(void*, void*, void*);
using MarkerUpdate = void(__fastcall*)(void*);
using ScopeInit = void*(__fastcall*)(void*, void*);
using ScopeApply = void(__fastcall*)(void*);
HitEvent g_originalHitEvent = nullptr;
MarkerUpdate g_originalMarkerUpdate = nullptr;
ScopeInit g_scopeInit = nullptr;
ScopeApply g_scopeApply = nullptr;
HookSlot g_hitEventHook;
HookSlot g_markerUpdateHook;
uintptr_t g_markerVtable = 0;
bool g_horizontalScaleCapped = true;

std::mutex g_projectionMutex;
NiPoint3 g_impact;
bool g_haveImpact = false;
NiPoint3 g_eye;
NiMatrix33 g_cameraWorld;
float g_frustumRight = 0.0f;
float g_frustumTop = 0.0f;
bool g_haveView = false;

uintptr_t g_marker = 0;
double g_baseX = 0.0;
double g_baseY = 0.0;
bool g_haveBase = false;

bool ReadImpact(void* sink, void* event, NiPoint3& point) {
    static std::atomic<uint64_t> faults{0};
    __try {
        const uintptr_t data = reinterpret_cast<uintptr_t>(event);
        // PlayerCharacter's TESHitEvent sink is at +0x4B0. The engine's
        // callback resolves the event's cause pointer before returning.
        if (*reinterpret_cast<const uint8_t*>(data + 0x100) == 0 ||
            *reinterpret_cast<const uintptr_t*>(data + 0xE8) !=
                reinterpret_cast<uintptr_t>(sink) - 0x4B0) return false;
        point = *reinterpret_cast<const NiPoint3*>(data);
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "hit impact", faults)) {
    }
    return false;
}

uint32_t __fastcall PlayerHitEvent(void* sink, void* event, void* source) {
    const uint32_t result = g_originalHitEvent(sink, event, source);
    NiPoint3 point;
    if (ReadImpact(sink, event, point)) {
        std::lock_guard<std::mutex> lock(g_projectionMutex);
        g_impact = point;
        g_haveImpact = true;
        static ULONGLONG lastLog = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - lastLog >= 1000) {
            lastLog = now;
            Log::Line("hit marker: impact world=(%.3f, %.3f, %.3f)", point.x, point.y, point.z);
        }
    }
    return result;
}

void __fastcall UpdateMarker(void* marker) {
    g_originalMarkerUpdate(marker);
    if (*static_cast<const uintptr_t*>(marker) != g_markerVtable) return;

    AimProjection projected{};
    bool havePoint = false;
    {
        std::lock_guard<std::mutex> lock(g_projectionMutex);
        havePoint = g_haveImpact && g_haveView;
        if (havePoint) {
            projected = ProjectWorldPointToNdc(g_impact, g_eye, g_cameraWorld,
                                                g_frustumRight, g_frustumTop);
        }
    }
    if (!havePoint) return;
    const CrosshairStageOffset offset = ComputeCrosshairStageOffset(
        true, projected.valid, projected.ndcX, projected.ndcY,
        GetViewportAspect(), g_horizontalScaleCapped);

    alignas(16) uint8_t scope[0x260]{};
    g_scopeInit(scope, marker);
    const double* current = reinterpret_cast<const double*>(scope + 0x10);
    if (g_marker != reinterpret_cast<uintptr_t>(marker)) {
        g_marker = reinterpret_cast<uintptr_t>(marker);
        g_haveBase = false;
    }
    if (!g_haveBase) {
        if (current[3] == 0.0 || current[4] == 0.0) return;
        g_baseX = current[0];
        g_baseY = current[1];
        g_haveBase = true;
        Log::Line("hit marker: authored position (%.2f, %.2f)", g_baseX, g_baseY);
    }
    double* pending = reinterpret_cast<double*>(scope + 0xF0);
    pending[0] = g_baseX + offset.dx;
    pending[1] = g_baseY + offset.dy;
    *reinterpret_cast<uint16_t*>(scope + 0x1C4) |= 3;
    g_scopeApply(scope);
}

uintptr_t FindHitEventCallback(uintptr_t moduleBase, const TextSection& text) {
    cameraunlock::memory::VtableInfo player{};
    if (!cameraunlock::memory::FindVtableFromRTTI(
            reinterpret_cast<void*>(moduleBase), "PlayerCharacter", player, 1)) return 0;
    const uint32_t type = *reinterpret_cast<const uint32_t*>(player.col_address + 12);
    uintptr_t start = 0;
    size_t size = 0;
    if (!FindSection(moduleBase, ".rdata", start, size)) return 0;
    uintptr_t locator = 0;
    for (size_t offset = 0; offset + 24 <= size; offset += 4) {
        const auto* col = reinterpret_cast<const uint32_t*>(start + offset);
        if (col[0] == 1 && col[1] == 0x4B0 && col[2] == 0 &&
            col[3] == type && moduleBase + col[5] == start + offset) {
            if (locator != 0) return 0;
            locator = start + offset;
        }
    }
    if (locator == 0) return 0;
    uintptr_t callback = 0;
    for (size_t offset = 0; offset + 24 <= size; offset += 8) {
        const auto* table = reinterpret_cast<const uintptr_t*>(start + offset);
        if (table[0] != locator) continue;
        if (callback != 0 || table[2] < text.start || table[2] >= text.start + text.size) return 0;
        callback = table[2];
    }
    return callback;
}

bool ReadView(uintptr_t niCamera, NiPoint3& eye, NiMatrix33& world,
              float& right, float& top) {
    static std::atomic<uint64_t> faults{0};
    __try {
        if (niCamera != 0) {
            eye = *WorldTranslationOf(niCamera);
            world = *WorldRotationOf(niCamera);
            right = *reinterpret_cast<const float*>(niCamera + NiCameraOffsets::FrustumRight);
            top = *reinterpret_cast<const float*>(niCamera + NiCameraOffsets::FrustumTop);
            return true;
        }
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "hit marker view", faults)) {
    }
    return false;
}

}

void PublishHitMarkerView(uintptr_t niCamera) {
    NiPoint3 eye;
    NiMatrix33 world;
    float right = 0.0f;
    float top = 0.0f;
    const bool valid = ReadView(niCamera, eye, world, right, top);
    std::lock_guard<std::mutex> lock(g_projectionMutex);
    g_haveView = valid;
    if (valid) {
        g_eye = eye;
        g_cameraWorld = world;
        g_frustumRight = right;
        g_frustumTop = top;
    }
}

void InstallHitMarkerHook(uintptr_t moduleBase, uintptr_t scopeInit,
                          uintptr_t scopeApply, bool horizontalScaleCapped) {
    TextSection text{};
    cameraunlock::memory::VtableInfo marker{};
    if (!FindTextSection(moduleBase, text) ||
        !cameraunlock::memory::FindVtableFromRTTI(
            reinterpret_cast<void*>(moduleBase), "FlashHitIndicator", marker, 5) ||
        marker.vfunc_count < 5) {
        Log::Line("ERROR: hit marker vtable not resolved");
        return;
    }
    const uintptr_t event = FindHitEventCallback(moduleBase, text);
    if (event == 0) {
        Log::Line("ERROR: player hit event callback not uniquely resolved");
        return;
    }
    if (!g_hitEventHook.Install(reinterpret_cast<void*>(event),
                               reinterpret_cast<void*>(&PlayerHitEvent),
                               reinterpret_cast<void**>(&g_originalHitEvent), "player hit event")) return;
    if (!g_markerUpdateHook.Install(reinterpret_cast<void*>(marker.vfuncs[4]),
                                   reinterpret_cast<void*>(&UpdateMarker),
                                   reinterpret_cast<void**>(&g_originalMarkerUpdate), "hit marker update")) {
        g_hitEventHook.Remove();
        return;
    }
    g_markerVtable = marker.vtable_address;
    g_scopeInit = reinterpret_cast<ScopeInit>(scopeInit);
    g_scopeApply = reinterpret_cast<ScopeApply>(scopeApply);
    g_horizontalScaleCapped = horizontalScaleCapped;
    Log::Line("hit marker: impact projection installed, event RVA 0x%llX update RVA 0x%llX",
              static_cast<unsigned long long>(event - moduleBase),
              static_cast<unsigned long long>(marker.vfuncs[4] - moduleBase));
}

void RemoveHitMarkerHook() {
    g_markerUpdateHook.Remove();
    g_hitEventHook.Remove();
}

}
