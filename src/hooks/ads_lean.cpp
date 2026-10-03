// SPDX-License-Identifier: MIT

#include "pch.h"
#include "ads_lean.h"
#include "camera_math.h"
#include "core/logging.h"
#include "core/seh_guard.h"

#include <cameraunlock/memory/rtti_vtable.h>

namespace Fallout4HT::AdsLean {
namespace {

// ActorState sits at Actor+0x128 and gunState is the 4-bit field at bit 14 of
// its second dword. FirstPersonState::Update itself tests this word for 6
// (sighted) or 8 (firing while sighted) to pick the iron-sights camera, on both
// 1.10.163 and the next-gen builds.
constexpr uintptr_t kActorStateWord = 0x134;
constexpr uint32_t kGunStateShift = 14;
constexpr uint32_t kGunStateMask = 0xF;
constexpr uint32_t kGunSighted = 6;
constexpr uint32_t kGunFireSighted = 8;

// PlayerCharacter::firstPerson3D.
constexpr uintptr_t kFirstPerson3D = 0xB78;
// TESCameraState::id; 0 is the first-person state, the one whose update reads
// its eye out of the first-person skeleton.
constexpr uintptr_t kCameraStateId = 0x20;
constexpr uint32_t kFirstPersonStateId = 0;

// NiAVObject world bound centre, moved with the node so culling agrees with it.
constexpr uintptr_t kWorldBoundCentre = 0xB0;
// NiNode children: an NiTArray whose data pointer is at +0x128 and capacity at +0x130.
constexpr uintptr_t kChildrenCapacity = 0x130;
// NiObject::IsNode(), which returns the object itself for an NiNode and null
// for anything else.
constexpr int kVtableIndex_IsNode = 4;

// The first-person skeleton with a weapon out is a few hundred nodes. These are
// bounds on a tree read out of engine memory, not expected sizes.
constexpr int kMaxDepth = 64;
constexpr int kMaxNodes = 8192;
constexpr uint16_t kMaxChildren = 1024;

// BSFlattenedBoneTree, the skeleton's 'Root' node. It keeps every bone in a flat
// table of its own (count at +0x140, entries at +0x148, 0xA0 bytes each, a local
// NiTransform followed by a world one), and only some bones also exist as scene
// nodes: 22 of the first-person skeleton's 111 on 1.11, 15 on 1.10.163. The arm
// skins read a bone's world transform from its node where it has one and from the
// table where it has none, which is every finger on 1.11 and every arm bone on
// 1.10.163. The weapon is rigid geometry under nodes. So the table has to move
// with the nodes, or the fingers (or the whole arms) stay behind while the weapon
// goes.
constexpr const char* kRTTI_FlattenedBoneTree = "BSFlattenedBoneTree";
constexpr uintptr_t kFlatBoneCount = 0x140;
constexpr uintptr_t kFlatBones = 0x148;
constexpr uintptr_t kFlatBoneSize = 0xA0;
constexpr uintptr_t kFlatBoneWorldTranslation = 0x40 + NiAVObjectOffsets::TranslateDelta;
constexpr int32_t kMaxFlatBones = 4096;

uintptr_t g_flattenedBoneTreeVtable = 0;

// Camera-thread only. File scope rather than a function static: a local static
// with a constructor makes the enclosing function require unwinding, which SEH
// forbids in the same function as a __try.
RigWrite g_lastRigWrite;

uintptr_t ReadPtr(uintptr_t address) { return *reinterpret_cast<const uintptr_t*>(address); }

void Translate(uintptr_t node, const NiPoint3& d) {
    NiPoint3* world = WorldTranslationOf(node);
    world->x += d.x;
    world->y += d.y;
    world->z += d.z;
    float* centre = reinterpret_cast<float*>(node + kWorldBoundCentre);
    centre[0] += d.x;
    centre[1] += d.y;
    centre[2] += d.z;

    if (ReadPtr(node) != g_flattenedBoneTreeVtable) return;
    const int32_t count = *reinterpret_cast<const int32_t*>(node + kFlatBoneCount);
    const uintptr_t bones = ReadPtr(node + kFlatBones);
    if (bones == 0 || count <= 0 || count > kMaxFlatBones) return;
    for (int32_t i = 0; i < count; ++i) {
        NiPoint3* bone = reinterpret_cast<NiPoint3*>(bones + i * kFlatBoneSize + kFlatBoneWorldTranslation);
        bone->x += d.x;
        bone->y += d.y;
        bone->z += d.z;
    }
}

bool IsNode(uintptr_t object) {
    using IsNode_t = uintptr_t(__fastcall*)(uintptr_t);
    const uintptr_t vtable = ReadPtr(object);
    return reinterpret_cast<IsNode_t>(ReadPtr(vtable + kVtableIndex_IsNode * sizeof(uintptr_t)))(object) != 0;
}

// A pure translation of the root moves every descendant's world translation by
// the same vector, so the whole tree is shifted without recomputing a transform.
void ShiftTree(uintptr_t node, const NiPoint3& d, int depth, int& visited) {
    if (node == 0 || depth > kMaxDepth || visited >= kMaxNodes) return;
    ++visited;
    Translate(node, d);
    if (!IsNode(node)) return;
    const uintptr_t children = ReadPtr(node + NiNodeOffsets::ChildrenData);
    const uint16_t capacity = *reinterpret_cast<const uint16_t*>(node + kChildrenCapacity);
    if (children == 0 || capacity > kMaxChildren) return;
    for (uint16_t i = 0; i < capacity; ++i) {
        ShiftTree(ReadPtr(children + i * sizeof(uintptr_t)), d, depth + 1, visited);
    }
}

}  // namespace

bool IsAiming(uintptr_t player) {
    if (player == 0) return false;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uint32_t gun =
            (*reinterpret_cast<const uint32_t*>(player + kActorStateWord) >> kGunStateShift) & kGunStateMask;
        return gun == kGunSighted || gun == kGunFireSighted;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "gun state", s_faults)) {
    }
    return false;
}

bool IsFirstPersonCamera(void* camera) {
    if (camera == nullptr) return false;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        const uintptr_t state =
            ReadPtr(reinterpret_cast<uintptr_t>(camera) + TESCameraOffsets::CurrentState);
        return state != 0 && *reinterpret_cast<const uint32_t*>(state + kCameraStateId) == kFirstPersonStateId;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "camera state", s_faults)) {
    }
    return false;
}

void Install(HMODULE gameModule) {
    cameraunlock::memory::VtableInfo info{};
    if (!cameraunlock::memory::FindVtableFromRTTI(gameModule, kRTTI_FlattenedBoneTree, info, 1)) {
        Log::Line("ERROR: ADS: no BSFlattenedBoneTree RTTI - the first-person skeleton cannot carry the"
                  " lean, so the lean eases out while aiming");
        return;
    }
    g_flattenedBoneTreeVtable = info.vtable_address;
}

uintptr_t FirstPersonRig(uintptr_t player) {
    if (player == 0 || g_flattenedBoneTreeVtable == 0) return 0;
    static std::atomic<uint64_t> s_faults{0};
    __try {
        return ReadPtr(player + kFirstPerson3D);
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "first-person rig", s_faults)) {
    }
    return 0;
}

void CarryOnRig(uintptr_t rig, const NiPoint3& world) {
    if (rig == 0) {
        g_lastRigWrite = RigWrite{};
        return;
    }
    static std::atomic<uint64_t> s_faults{0};
    __try {
        // The skeleton's parent is the identity, so its local translation is in
        // the same world-aligned frame as the offset. The tree is shifted as well
        // because the engine has already brought its world transforms up to date
        // this frame, and the camera update about to run reads the eye from them.
        NiPoint3* local = LocalTranslationOf(rig);
        const NiPoint3 delta = RigDelta(g_lastRigWrite, rig, *local, world);
        if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f) {
            local->x += delta.x;
            local->y += delta.y;
            local->z += delta.z;
            int visited = 0;
            ShiftTree(rig, delta, 0, visited);
        }
        g_lastRigWrite.rig = rig;
        g_lastRigWrite.applied = world;
        g_lastRigWrite.localAfter = *local;
    } __except (SehAbsorbAccessViolation(GetExceptionCode(), "rig lean", s_faults)) {
        g_lastRigWrite = RigWrite{};
    }
}

}  // namespace Fallout4HT::AdsLean
