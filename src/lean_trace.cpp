// SPDX-License-Identifier: MIT
#include "pch.h"
#include "lean_trace.h"
#include "core/mod.h"
#include "core/logging.h"
#include "hooks/module_scan.h"
#include "hooks/player_hook.h"
#include "hooks/activation_hook.h"
#include "hooks/collision_math.h"
#include "hooks/hook_slot.h"

namespace Fallout4HT::lean_trace {
namespace {
struct alignas(16) PickData { unsigned char bytes[0xE0]; };
using Construct = void* (*)(PickData*);
using SetRay = void (*)(PickData*, const NiPoint3*, const NiPoint3*);
using Pick = void* (*)(void*, PickData*);
Construct g_construct = nullptr;
SetRay g_setRay = nullptr;
Pick g_pick = nullptr;
// The engine's pick behind this mod's hook on it: the mod's own rays go straight here.
Pick g_originalPick = nullptr;
HookSlot g_pickHook;
cameraunlock::camera::LeanClamp g_clamp;
uintptr_t g_lastCamera = 0;
uintptr_t g_lastState = 0;
void* g_lastCell = nullptr;
uint64_t g_lastTick = 0;
cameraunlock::math::Vec3 g_lastEye;
cameraunlock::math::Vec3 g_lastDesired;
float g_lastMargin = 0;
float g_lastScale = 1;
bool g_haveEye = false;
uint64_t g_lastReport = 0;
bool g_lastFailed = false;
bool g_lastContact = false;
uint32_t g_changesSinceReport = 0;
bool g_prevFailed = false;
bool g_prevContact = false;

// A head held still next to a wall puts sub-unit tracker noise either side of
// the clamp's minimum lean, so contact can flip every frame: one session logged
// 40 lines a second from the game thread. A change is still reported within a
// second, with the flips in between counted on that line.
constexpr uint64_t kMinChangeReportMs = 1000;
constexpr uint64_t kSampleReportMs = 5000;

struct QueryContext { void* cell; float margin; int channel; };

// The collision layer the game's projectiles are on.
constexpr uint32_t kProjectileChannel = 6;

// How many bodies that are not level geometry one lean ray passes through, and
// how far past each it starts again.
constexpr int kMaxBodiesPassed = 8;
constexpr float kPassStepUnits = 1.0f;

struct RayResult { bool queried; bool hit; float fraction; float cosine; uint32_t filter; };
// The last lean ray that hit, for the log line.
float g_hitDistance = 0;
float g_hitCosine = 0;
uint32_t g_hitFilter = 0;

template<class T> T& Field(PickData& data, size_t offset) {
    return *reinterpret_cast<T*>(data.bytes + offset);
}

void ReleaseWorld(PickData& data) {
    const uintptr_t world = Field<uintptr_t>(data, 0xC0);
    if (!world || *reinterpret_cast<const uint16_t*>(world + 0xA) == 0) return;
    // Havok stores the reference count in the low half of its allocation word.
    if (_InterlockedDecrement16(reinterpret_cast<volatile short*>(world + 8)) == 0) {
        const auto vtable = *reinterpret_cast<uintptr_t**>(world);
        reinterpret_cast<void (*)(void*)>(vtable[3])(reinterpret_cast<void*>(world));
    }
}

RayResult CastRay(void* cell, const NiPoint3& from, const NiPoint3& to, const cameraunlock::math::Vec3& direction,
                  uint32_t filter) {
    if (!g_construct || !g_setRay || !g_pick || !cell) return {};
    PickData data{};
    g_construct(&data);
    g_setRay(&data, &from, &to);
    Field<uint32_t>(data, 0xC) = filter;
    // Render-only queries neither consume the gameplay pick budget nor skip when it is spent.
    data.bytes[0xDC] = 0;
    data.bytes[0xDD] = 0;
    (g_originalPick ? g_originalPick : g_pick)(cell, &data);
    RayResult result;
    result.queried = Field<uintptr_t>(data, 0xC0) != 0 && data.bytes[0xDE] == 0;
    result.hit = Field<uint32_t>(data, 0xBC) != 0;
    result.fraction = Field<float>(data, 0x80) / Field<float>(data, 0x3C);
    result.cosine = direction.x * Field<float>(data, 0x70) +
        direction.y * Field<float>(data, 0x74) + direction.z * Field<float>(data, 0x78);
    result.filter = Field<uint32_t>(data, 0xAC);
    ReleaseWorld(data);
    if (result.hit && (!std::isfinite(result.fraction) || !std::isfinite(result.cosine) || result.fraction < 0 ||
                       result.fraction > 1)) {
        result.queried = false;
    }
    return result;
}

// The collision group of the player's own bodies, learned from the engine: a pick
// it casts from the player's eye carries that group, which is how its own picks
// pass through the player. Zero until one has been seen.
std::atomic<uint32_t> g_playerGroup{0};
std::atomic<uint64_t> g_groupLearnedMs{0};
constexpr uint64_t kGroupRelearnMs = 1000;
constexpr float kHavokUnitsPerGameUnit = 0.0142875f;
constexpr float kEyeToleranceUnits = 0.5f;

bool StartsAt(const float* origin, const NiPoint3& eye) {
    const float dx = origin[0] / kHavokUnitsPerGameUnit - eye.x;
    const float dy = origin[1] / kHavokUnitsPerGameUnit - eye.y;
    const float dz = origin[2] / kHavokUnitsPerGameUnit - eye.z;
    return dx * dx + dy * dy + dz * dz <= kEyeToleranceUnits * kEyeToleranceUnits;
}

void* PickHook(void* cell, PickData* data) {
    const uint32_t group = Field<uint32_t>(*data, 0xC) >> 16;
    const uint64_t now = GetTickCount64();
    if (group != 0 && now - g_groupLearnedMs.load(std::memory_order_relaxed) >= kGroupRelearnMs) {
        AimReference reference;
        const float* origin = &Field<float>(*data, 0x20);
        if (GetAimReference(reference) &&
            (StartsAt(origin, reference.cleanEye) || StartsAt(origin, reference.trackedEye))) {
            if (g_playerGroup.exchange(group, std::memory_order_relaxed) != group) {
                Log::Line("collision: the player's collision group is %u", group);
            }
            g_groupLearnedMs.store(now, std::memory_order_relaxed);
        }
    }
    return g_originalPick(cell, data);
}

void* PlayerCell() {
    void* player = PlayerActor();
    return player ? *reinterpret_cast<void**>(static_cast<unsigned char*>(player) + 0xB8) : nullptr;
}
}

void Initialize() {
    const int channel = Mod::Instance().Settings().collision_channel;
    if (channel < 0 || channel >= 64) {
        Log::Line("ERROR: collision unavailable: CollisionChannel=%d must be between 0 and 63", channel);
        return;
    }
    TextSection text{};
    if (!FindTextSection(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), text)) {
        Log::Line("ERROR: collision unavailable: executable text section not found");
        return;
    }
    const uint8_t ctor[] = {0x33,0xD2,0xB8,0xFF,0xFF,0,0,0x0F,0x57,0xC0,
        0x66,0x89,0x41,0x08,0x89,0x51,0x0C,0x48,0x89,0x51,0x10};
    const uint8_t ray[] = {0x48,0x8B,0xC4,0x48,0x83,0xEC,0x38,0xF3,0x0F,0x10,0x15,
        0,0,0,0,0xC7,0x40,0xE4,0,0,0,0,0x0F,0x29,0x70,0xE8};
    const uint8_t pick[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x44,0x8B,0x05,
        0,0,0,0,0x65,0x48,0x8B,0x04,0x25,0x58,0,0,0,0x48,0x8B,0xE9,
        0x4A,0x8B,0x3C,0xC0,0xB9,0xC0,0x09,0,0,0x48,0x8B,0xF2,
        0x48,0x03,0xF9,0x8B,0x1F,0xC7,0x07,0x32,0,0,0,0xF6,0x45,0x40,0x01};
    const uint8_t ctorNew[] = {0x33,0xD2,0xB8,0xFF,0xFF,0,0,0x66,0x89,0x41,0x08,
        0x0F,0x57,0xC0,0x89,0x51,0x0C,0x48,0x89,0x51,0x10,0x48,0x89,0x11,0x88,0x51,0x18};
    const uint8_t rayNew[] = {0x48,0x8B,0xC4,0x48,0x83,0xEC,0x48,0xF3,0x0F,0x10,0x0D,
        0,0,0,0,0xF3,0x41,0x0F,0x10,0x10,0xF3,0x0F,0x10,0x2A,0xF3,0x41,0x0F,0x10,0x58,0x04};
    const uint8_t pickNew[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x44,0x8B,0x05,
        0,0,0,0,0x48,0x8B,0xE9,0x65,0x48,0x8B,0x04,0x25,0x58,0,0,0,
        0x48,0x8B,0xF2,0xB9,0xC0,0x09,0,0,0x4A,0x8B,0x3C,0xC0,0x48,0x03,0xF9,
        0x8B,0x1F,0xC7,0x07,0x32,0,0,0,0xF6,0x45,0x40,0x01};
    const char* matched;
    g_construct = reinterpret_cast<Construct>(FindUniquePatternEither(text, ctor,
        "xxxxxxxxxxxxxxxxxxxxx", "1.10.163", ctorNew, "xxxxxxxxxxxxxxxxxxxxxxxxxxx",
        "1.11", "collision pick constructor", matched));
    g_setRay = reinterpret_cast<SetRay>(FindUniquePatternEither(text, ray,
        "xxxxxxxxxxx????xxxxxxxxxxx", "1.10.163", rayNew, "xxxxxxxxxxx????xxxxxxxxxxxxxxx",
        "1.11", "collision ray endpoints", matched));
    g_pick = reinterpret_cast<Pick>(FindUniquePatternEither(text, pick,
        "xxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "1.10.163",
        pickNew, "xxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        "1.11", "collision cell pick", matched));
    Log::Line("%s: collision query constructor=%p endpoints=%p pick=%p",
        g_construct && g_setRay && g_pick ? "READY" : "ERROR: collision unavailable",
        reinterpret_cast<void*>(g_construct), reinterpret_cast<void*>(g_setRay),
        reinterpret_cast<void*>(g_pick));
    if (g_pick) {
        g_pickHook.Install(reinterpret_cast<void*>(g_pick), reinterpret_cast<void*>(&PickHook),
                           reinterpret_cast<void**>(&g_originalPick), "cell pick");
    }
}

cameraunlock::camera::LeanObstruction Query(void* context,
    const cameraunlock::math::Vec3& start, const cameraunlock::math::Vec3& direction,
    float maxDistance) {
    const auto& query = *static_cast<QueryContext*>(context);
    // Overreach for glancing approaches, with the same cosine floor as the standoff.
    const float range = CollisionTraceRange(maxDistance, query.margin);
    // A body that is not level geometry is passed through: the ray starts again
    // just past where it met it. A ray that starts inside a body does not hit it.
    float travelled = 0.0f;
    for (int pass = 0; pass < kMaxBodiesPassed; ++pass) {
        const NiPoint3 from(start.x + direction.x * travelled, start.y + direction.y * travelled,
                            start.z + direction.z * travelled);
        const NiPoint3 to(start.x + direction.x * range, start.y + direction.y * range,
                          start.z + direction.z * range);
        const RayResult ray = CastRay(query.cell, from, to, direction, static_cast<uint32_t>(query.channel));
        if (!ray.queried) return {};
        if (!ray.hit) return {true, false, 0};
        const float distance = travelled + ray.fraction * (range - travelled);
        g_hitDistance = distance;
        g_hitCosine = ray.cosine;
        g_hitFilter = ray.filter;
        if (CollisionLayerBlocksLean(ray.filter)) {
            return {true, true, CollisionHitAllowance(distance, query.margin, ray.cosine)};
        }
        travelled = distance + kPassStepUnits;
        if (travelled >= range) return {true, false, 0};
    }
    return {true, false, 0};
}

bool AimRayHit(const NiPoint3& start, const NiPoint3& direction, float range, float& distance) {
    const NiPoint3 to(start.x + direction.x * range, start.y + direction.y * range, start.z + direction.z * range);
    // The round passes through the player's own body, and so does this ray. Until
    // the group is known there is no telling the player's arm from a wall.
    const uint32_t group = g_playerGroup.load(std::memory_order_relaxed);
    if (group == 0) return false;
    const RayResult ray = CastRay(PlayerCell(), start, to,
                                  cameraunlock::math::Vec3(direction.x, direction.y, direction.z),
                                  kProjectileChannel | (group << 16));
    if (!ray.queried || !ray.hit) return false;
    distance = ray.fraction * range;
    return true;
}

void Reset() {
    g_clamp.Reset();
    g_haveEye = false;
    g_lastCamera = 0;
    g_lastCell = nullptr;
    g_lastTick = 0;
}

float Clamp(const NiPoint3& eye, const NiPoint3& offset, uintptr_t camera,
            uintptr_t state, float nearPlane, uint64_t tick, float deltaTime) {
    const auto& config = Mod::Instance().Settings();
    if (!config.collision_enabled) { Reset(); return 1.0f; }
    void* cell = PlayerCell();
    const cameraunlock::math::Vec3 clean(eye.x, eye.y, eye.z);
    const cameraunlock::math::Vec3 desired(offset.x, offset.y, offset.z);
    auto settings = config.lean_clamp;
    settings.skin = std::max(settings.skin, nearPlane + 1.0f);
    if (g_haveEye && tick == g_lastTick && camera == g_lastCamera &&
        state == g_lastState && cell == g_lastCell && settings.skin == g_lastMargin &&
        clean.x == g_lastEye.x && clean.y == g_lastEye.y && clean.z == g_lastEye.z &&
        desired.x == g_lastDesired.x && desired.y == g_lastDesired.y && desired.z == g_lastDesired.z) {
        return g_lastScale;
    }
    if (camera != g_lastCamera || state != g_lastState || cell != g_lastCell ||
        (g_haveEye && (clean - g_lastEye).Magnitude() > 128.0f)) g_clamp.Reset();
    g_lastCamera = camera;
    g_lastState = state;
    g_lastCell = cell;
    g_lastEye = clean;
    g_lastDesired = desired;
    g_lastMargin = settings.skin;
    g_haveEye = true;
    g_clamp.SetSettings(settings);
    QueryContext query{cell, settings.skin, config.collision_channel};
    const auto allowed = g_clamp.Apply(clean, desired, tick == g_lastTick ? 0.0f : deltaTime, Query, &query);
    g_lastTick = tick;
    const uint64_t now = GetTickCount64();
    const bool failed = g_clamp.LastQueryFailed();
    const bool contact = g_clamp.InContact();
    if (failed != g_prevFailed || contact != g_prevContact) ++g_changesSinceReport;
    g_prevFailed = failed;
    g_prevContact = contact;
    const uint64_t sinceReport = now - g_lastReport;
    const bool changed = failed != g_lastFailed || contact != g_lastContact;
    if ((changed && sinceReport >= kMinChangeReportMs) || sinceReport >= kSampleReportMs) {
        Log::Line("collision: queried=%s contact=%s desired=%.3f allowed=%.3f margin=%.3f near=%.3f channel=%d eye=(%.2f,%.2f,%.2f) changes=%u | last hit %.1f units off, cosine %.3f, filter %08X",
            failed ? "FAILED" : "ok", contact ? "yes" : "no", desired.Magnitude(),
            allowed.Magnitude(), settings.skin, nearPlane, config.collision_channel, eye.x, eye.y, eye.z,
            g_changesSinceReport, g_hitDistance, g_hitCosine, g_hitFilter);
        g_lastReport = now;
        g_lastFailed = failed;
        g_lastContact = contact;
        g_changesSinceReport = 0;
    }
    const float length = desired.Magnitude();
    g_lastScale = length > 0.00001f ? allowed.Magnitude() / length : 1.0f;
    return g_lastScale;
}
}
