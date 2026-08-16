// SPDX-License-Identifier: MIT

#include "pch.h"
#include "hook_slot.h"
#include "core/logging.h"

#include <cameraunlock/hooks/hook_manager.h>

namespace Fallout4HT {

bool HookSlot::Install(void* target, void* detour, void** original, const char* label) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;

    const HookStatus status = HookManager::Instance().CreateHook(target, detour, original);
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: CreateHook(%s) failed: %s", label, HookStatusToString(status));
        return false;
    }

    m_target = target;
    m_original = original;
    return true;
}

void HookSlot::Remove() {
    if (!m_target) return;

    using cameraunlock::hooks::HookManager;
    HookManager::Instance().DisableHook(m_target);
    HookManager::Instance().RemoveHook(m_target);

    m_target = nullptr;
    if (m_original) {
        *m_original = nullptr;
        m_original = nullptr;
    }
}

} // namespace Fallout4HT
