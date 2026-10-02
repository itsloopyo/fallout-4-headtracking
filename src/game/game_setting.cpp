// SPDX-License-Identifier: MIT

#include "pch.h"

#include "game/game_setting.h"
#include "core/seh_guard.h"
#include "hooks/module_scan.h"

#include <cstring>

namespace Fallout4HT {

const char* FindSettingName(uintptr_t moduleBase, const char* name) {
    const size_t len = std::strlen(name);
    for (const char* section : {".rdata", ".data"}) {
        uintptr_t start = 0;
        size_t size = 0;
        if (!FindSection(moduleBase, section, start, size)) continue;
        if (size < len + 1) continue;
        static std::atomic<uint64_t> s_faults{0};
        __try {
            for (size_t off = 0; off + len + 1 <= size; ++off) {
                const char* at = reinterpret_cast<const char*>(start + off);
                if (at[0] != name[0]) continue;
                if (std::memcmp(at, name, len + 1) == 0) return at;
            }
        } __except (SehAbsorbAccessViolation(GetExceptionCode(), "setting name scan", s_faults)) {
        }
    }
    return nullptr;
}

} // namespace Fallout4HT
