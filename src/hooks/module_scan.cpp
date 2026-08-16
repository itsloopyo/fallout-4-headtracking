// SPDX-License-Identifier: MIT

#include "pch.h"
#include "module_scan.h"

namespace Fallout4HT {

bool FindTextSection(uintptr_t moduleBase, TextSection& out) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(moduleBase);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        // Exact match: ".textbss" is a different section that also passes a
        // 5-byte prefix compare.
        if (std::memcmp(sections[i].Name, ".text\0\0\0", IMAGE_SIZEOF_SHORT_NAME) != 0) continue;
        out.start = moduleBase + sections[i].VirtualAddress;
        out.size = sections[i].Misc.VirtualSize;
        return true;
    }
    return false;
}

} // namespace Fallout4HT
