// SPDX-License-Identifier: MIT

#include "pch.h"
#include "module_scan.h"

namespace Fallout4HT {

bool FindSection(uintptr_t moduleBase, const char* name, uintptr_t& start, size_t& size) {
    char wanted[IMAGE_SIZEOF_SHORT_NAME] = {};
    for (int i = 0; i < IMAGE_SIZEOF_SHORT_NAME && name[i] != '\0'; ++i) wanted[i] = name[i];

    const uint8_t* base = reinterpret_cast<const uint8_t*>(moduleBase);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sections[i].Name, wanted, IMAGE_SIZEOF_SHORT_NAME) != 0) continue;
        start = moduleBase + sections[i].VirtualAddress;
        size = sections[i].Misc.VirtualSize;
        return true;
    }
    return false;
}

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
