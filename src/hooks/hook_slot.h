// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// One MinHook registration and the trampoline pointer that goes with it. Owning
// both together is what makes teardown safe to repeat: Remove() clears the
// trampoline as well as the registration, so a detour that survives the removal
// cannot call through a dangling original.
class HookSlot {
public:
    // Logs the failure itself, using `label` for the diagnostic. `original`
    // receives the trampoline and is cleared again by Remove().
    bool Install(void* target, void* detour, void** original, const char* label);

    // Idempotent - a slot that never installed, or already came out, is a no-op.
    void Remove();

    bool IsInstalled() const { return m_target != nullptr; }

private:
    void* m_target = nullptr;
    void** m_original = nullptr;
};

} // namespace Fallout4HT
