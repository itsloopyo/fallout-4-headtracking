// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pattern_scanner.h>

#include "core/logging.h"

namespace Fallout4HT {

// The executable code range of a loaded module. Patterns are only ever searched
// here: scanning the whole image would match constant pools and RTTI strings.
struct TextSection {
    uintptr_t start;
    size_t size;
};

// Locate the module's .text section. Returns false if the image has none.
bool FindTextSection(uintptr_t moduleBase, TextSection& out);

// Locate any section by name, exact match on the full 8 bytes so ".data" does
// not also answer for ".data1".
bool FindSection(uintptr_t moduleBase, const char* name, uintptr_t& start, size_t& size);

// Locate a prologue in .text, refusing to return anything if more than one site
// matches. The functions hooked by this mod carry no RTTI, so a pattern is the
// only anchor available, and one that has silently become ambiguous must fail
// loudly rather than hook an arbitrary one of the matches.
//
// The mask is taken by reference to an array of exactly N+1 chars, so a mask out
// of step with its pattern is a compile error rather than a silently truncated
// comparison. Its contents are checked at startup for the same reason: the
// scanner treats any character other than 'x' as a wildcard, so a typo would
// quietly widen the pattern instead of failing.
template <size_t N>
uintptr_t FindUniquePattern(const TextSection& text,
                            const uint8_t (&pattern)[N],
                            const char (&mask)[N + 1], const char* label) {
    using cameraunlock::memory::ScanPatternMaskInRange;

    for (size_t i = 0; i < N; ++i) {
        if (mask[i] != 'x' && mask[i] != '?') {
            Log::Line("ERROR: %s mask has '%c' at %zu - only 'x' and '?' are meaningful",
                      label, mask[i], i);
            return 0;
        }
    }

    void* first = ScanPatternMaskInRange(text.start, text.size, pattern, mask, N);
    if (!first) return 0;

    const uintptr_t hit = reinterpret_cast<uintptr_t>(first);
    const uintptr_t after = hit + 1;
    if (after < text.start + text.size &&
        ScanPatternMaskInRange(after, text.start + text.size - after, pattern, mask, N)) {
        Log::Line("ERROR: %s pattern matches more than one site - refusing to hook", label);
        return 0;
    }
    return hit;
}

// The same function can be scheduled differently by different game builds, so a
// hook carries a prologue per build shape. They are tried in order and the first
// unique match wins; `matched` names the one that did, which is what a log line
// needs to say which build the player is on.
//
// Deliberately NOT a fingerprint-keyed table of addresses. RVAs move on every
// relink and would strand players on any build nobody has added yet, whereas a
// prologue survives a patch that only shifts code around - which is why nothing
// in this mod is pinned per build.
template <size_t N1, size_t N2>
uintptr_t FindUniquePatternEither(const TextSection& text,
                                  const uint8_t (&first)[N1], const char (&firstMask)[N1 + 1],
                                  const char* firstName,
                                  const uint8_t (&second)[N2], const char (&secondMask)[N2 + 1],
                                  const char* secondName,
                                  const char* label, const char*& matched) {
    if (const uintptr_t hit = FindUniquePattern(text, first, firstMask, label)) {
        matched = firstName;
        return hit;
    }
    if (const uintptr_t hit = FindUniquePattern(text, second, secondMask, label)) {
        matched = secondName;
        return hit;
    }
    matched = "none";
    return 0;
}

} // namespace Fallout4HT
