// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

// Fallout 4 1.10.163 (Creation Engine / NetImmerse) camera layout.
//
// Discovery was STATIC (no game launch). Sources:
//   - Struct offsets: CommonLibF4 (Ryan-rsm-McKenzie) + CommonLibSSE-NG cross-check.
//   - PlayerCamera / PlayerCharacter vtables + Update slots: static RTTI scan of
//     the on-disk EXE (scripts/static_rtti_scan.py). SteamStub (.bind) wraps only
//     .text, so the RTTI/vtables in .rdata are plaintext and vtable slots hold
//     valid function RVAs even though the .text they point at is encrypted at rest.
//
// The mod resolves both vtables at RUNTIME via RTTI (DRM-immune, patch-
// independent); the RVAs below are recorded for reference/triage only. The
// struct offsets are stable across Fallout 4 patches (NetImmerse layout is
// fixed) - only a PE fingerprint failsafe is needed to confirm "this is FO4".
//
// NetImmerse render-path convention: X=forward, Y=up, Z=right.
// World units: ~70 units per metre (Bethesda standard, same as Skyrim).

namespace Fallout4HT {

// Bethesda standard, same as Skyrim: the engine's world units per metre. Every
// tracker offset (which arrives in metres) is scaled by this before it reaches a
// scene-graph translation.
inline constexpr float kUnitsPerMeter = 70.0f;

struct NiPoint3 {
    float x, y, z;

    NiPoint3() : x(0), y(0), z(0) {}
    NiPoint3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// Row-major 3x3 rotation matrix (NiMatrix3).
//
// FO4 pads every row to 4 floats, so the struct is 0x30 bytes, not the 0x24 a
// bare float[3][3] would give. Column 3 is padding and reads 0 in the engine's
// own matrices; keep writing 0 there so a matrix we store is byte-identical to
// one the engine stored. Getting this wrong lays 9 floats across a 12-float
// block, mixing rotation rows with pad columns - the result is no longer
// orthonormal and the rendered view shears.
struct NiMatrix33 {
    float entry[3][4];

    NiMatrix33() { SetIdentity(); }

    void SetIdentity() {
        entry[0][0] = 1; entry[0][1] = 0; entry[0][2] = 0; entry[0][3] = 0;
        entry[1][0] = 0; entry[1][1] = 1; entry[1][2] = 0; entry[1][3] = 0;
        entry[2][0] = 0; entry[2][1] = 0; entry[2][2] = 1; entry[2][3] = 0;
    }

    // ZXY rotation order (yaw * pitch * roll).
    void SetFromEulerAngles(float yaw, float pitch, float roll) {
        const float cy = cosf(yaw),   sy = sinf(yaw);
        const float cp = cosf(pitch), sp = sinf(pitch);
        const float cr = cosf(roll),  sr = sinf(roll);
        entry[0][0] = cy * cr - sy * sp * sr;
        entry[0][1] = -sy * cp;
        entry[0][2] = cy * sr + sy * sp * cr;
        entry[1][0] = sy * cr + cy * sp * sr;
        entry[1][1] = cy * cp;
        entry[1][2] = sy * sr - cy * sp * cr;
        entry[2][0] = -cp * sr;
        entry[2][1] = sp;
        entry[2][2] = cp * cr;
        entry[0][3] = entry[1][3] = entry[2][3] = 0;
    }

    // Skips the default ctor's SetIdentity() since every entry is overwritten.
    static NiMatrix33 FromEulerAngles(float yaw, float pitch, float roll) {
        NiMatrix33 m;
        m.SetFromEulerAngles(yaw, pitch, roll);
        return m;
    }

    NiMatrix33 operator*(const NiMatrix33& other) const {
        NiMatrix33 result;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++)
                result.entry[i][j] =
                    entry[i][0] * other.entry[0][j] +
                    entry[i][1] * other.entry[1][j] +
                    entry[i][2] * other.entry[2][j];
            result.entry[i][3] = 0;
        }
        return result;
    }

    // Rows of a node's world rotation are its world-space axes (row0 = right,
    // row1 = forward, row2 = up), so a vector expressed in node-local
    // components is recombined FROM the rows - not multiplied through them.
    NiPoint3 LocalToWorld(const NiPoint3& v) const {
        return NiPoint3(
            entry[0][0] * v.x + entry[1][0] * v.y + entry[2][0] * v.z,
            entry[0][1] * v.x + entry[1][1] * v.y + entry[2][1] * v.z,
            entry[0][2] * v.x + entry[1][2] * v.y + entry[2][2] * v.z
        );
    }
};
static_assert(sizeof(NiMatrix33) == 0x30, "NiMatrix33 size mismatch");

// Row-major 4x4 matrix. worldToCam is read for diagnostics only - the engine
// rebuilds it from cameraRoot after the camera hook returns, so head tracking
// never writes it.
struct NiMatrix44 {
    float entry[4][4];
};
static_assert(sizeof(NiMatrix44) == 0x40, "NiMatrix44 size mismatch");

// --- Class names for runtime RTTI discovery ----------------------------------
// Plain class names: cameraunlock's FindVtableFromRTTI builds the mangled
// ".?AV<name>@@" form itself.
constexpr const char* kRTTI_PlayerCamera     = "PlayerCamera";
constexpr const char* kRTTI_PlayerCharacter  = "PlayerCharacter";
// The camera state PlayerCamera switches to for the VATS attack sequence - the
// slow-motion shot the game frames for itself once the player commits. Unlike
// the targeting menu, which changes no camera state at all, this one is a state
// swap the mod can see, so the gate for it needs nothing pinned per build.
constexpr const char* kRTTI_VATSCameraState  = "VATSCameraState";
// The VATS singleton itself. It is a global object, so its vtable pointer sits
// in .data and finding it there anchors the targeting-menu flag to an object
// rather than to a raw address a patch moves.
constexpr const char* kRTTI_VATS             = "VATS";

// Actor::Update, from CommonLibF4 (Skyrim uses 0xAD). The player's update is
// where gameplay reads the camera - interaction raycasts and, in third person,
// the camera-collision machinery - so it is the sandwich point that keeps game
// logic looking at the body's camera rather than the head's.
constexpr int kVtableIndex_ActorUpdate = 0xCF;

// TESCamera primary vtable (CommonLibF4): 00 ~dtor, 01 SetCameraRoot,
// 02 SetEnabled, 03 Update. NOTE: Skyrim SE has Update at index 2 - FO4 added a
// virtual SetEnabled at 2, pushing Update to 3. PlayerCamera does NOT override
// Update, so hooking by function address catches every TESCamera-derived camera;
// the camera hook filters on the object's vtable == PlayerCamera vtable.
constexpr int kVtableIndex_TESCameraUpdate = 3;

// --- NiAVObject (base of every scene-graph node), F4 size 0x120 -------------
// NiTransform layout: { NiMatrix3 rotate (0x30 - rows padded to 4 floats),
// NiPoint3 translate (@+0x30), float scale (@+0x3C) }, so the whole transform
// is 0x40 and local -> world is a 0x40 step. Skyrim packs NiMatrix3 to 0x24 and
// steps 0x34; the translate delta differs for the same reason.
// Confirmed at runtime by dumping a live cameraRoot: rotation rows read
// orthonormal with a 0 pad column, translate holds the camera world position,
// and scale reads 1.0.
namespace NiAVObjectOffsets {
    constexpr uintptr_t LocalTransform = 0x30;   // Skyrim: 0x48
    constexpr uintptr_t WorldTransform = 0x70;   // Skyrim: 0x7C
    // Offset of the translate WITHIN an NiTransform, so it applies to the local
    // and the world transform alike.
    constexpr uintptr_t TranslateDelta = 0x30;   // Skyrim: 0x24
}

// The offsets above are only ever dereferenced through these, so every node
// access in the mod reads the same way and a layout fix lands in one place.
inline NiMatrix33* LocalRotationOf(uintptr_t node) {
    return reinterpret_cast<NiMatrix33*>(node + NiAVObjectOffsets::LocalTransform);
}

inline NiMatrix33* WorldRotationOf(uintptr_t node) {
    return reinterpret_cast<NiMatrix33*>(node + NiAVObjectOffsets::WorldTransform);
}

inline NiPoint3* LocalTranslationOf(uintptr_t node) {
    return reinterpret_cast<NiPoint3*>(
        node + NiAVObjectOffsets::LocalTransform + NiAVObjectOffsets::TranslateDelta);
}

inline NiPoint3* WorldTranslationOf(uintptr_t node) {
    return reinterpret_cast<NiPoint3*>(
        node + NiAVObjectOffsets::WorldTransform + NiAVObjectOffsets::TranslateDelta);
}

// --- NiNode (extends NiAVObject) --------------------------------------------
// children NiTObjectArray base @0x120; the T* data pointer is at base+0x08.
namespace NiNodeOffsets {
    constexpr uintptr_t ChildrenData = 0x128;         // Skyrim: 0x118
}

// --- NiCamera (extends NiAVObject) ------------------------------------------
// worldToCam is the first member after the 0x120 NiAVObject; NiFrustum follows
// the 0x40 matrix. (left/right/top/bottom normalised to near=1, so right ==
// tan(hFOV/2), top == tan(vFOV/2).)
//
// Confirmed at runtime against a live NiCamera: the NiFrustum immediately after
// the 0x40 worldToCam reads left/right/top/bottom = -1.61781/+1.61781/+0.45501/
// -0.45501 - symmetric on both axes, which only lines up if worldToCam really
// starts at 0x120.
namespace NiCameraOffsets {
    constexpr uintptr_t WorldToCam   = 0x120;         // Skyrim: 0x110
    constexpr uintptr_t FrustumRight = 0x164;         // Skyrim: 0x154
    constexpr uintptr_t FrustumTop   = 0x168;         // Skyrim: 0x158
}

// --- TESCamera --------------------------------------------------------------
// cameraRoot (NiPointer<NiNode>) @0x20 - identical to Skyrim.
namespace TESCameraOffsets {
    constexpr uintptr_t CameraRoot = 0x20;
    // currentState (NiPointer<TESCameraState>) follows cameraRoot. Its vtable
    // identifies which camera the game is running - first person, third person,
    // VATS, furniture, and so on - which is both how a run proves which view it
    // measured and how "the view keeps flipping between two cameras" would be
    // told apart from "one camera keeps moving".
    constexpr uintptr_t CurrentState = 0x28;
}

// The two scene-graph nodes head tracking writes each frame.
struct CameraNodes {
    uintptr_t cameraRoot;
    uintptr_t niCamera;
};

// Walk from a TESCamera to both cameraRoot and the NiCamera child.
// Path: TESCamera+0x20 to cameraRoot (NiNode) to children[0] to NiCamera.
// Dereferences engine pointers - the caller owns the guard.
inline bool ResolveCameraNodes(void* camera, CameraNodes& out) {
    out.cameraRoot = *reinterpret_cast<uintptr_t*>(
        reinterpret_cast<uintptr_t>(camera) + TESCameraOffsets::CameraRoot);
    if (out.cameraRoot == 0) return false;

    uintptr_t childData = *reinterpret_cast<uintptr_t*>(out.cameraRoot + NiNodeOffsets::ChildrenData);
    if (childData == 0) return false;

    out.niCamera = *reinterpret_cast<uintptr_t*>(childData);  // children[0]
    return out.niCamera != 0;
}

} // namespace Fallout4HT
