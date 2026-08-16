// SPDX-License-Identifier: MIT

#include "pch.h"
#include "seh_guard.h"
#include "logging.h"

namespace Fallout4HT {

LONG SehAbsorbAnyException(const char* where, std::atomic<uint64_t>& count) {
    const uint64_t n = count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) == 0) {
        // EmergencyLine, not Line: a filter runs on the faulting thread, which
        // may be the one already holding the log mutex. It still goes through
        // the CRT and a synchronous flush, so the rate limit is load-bearing.
        Log::EmergencyLine("WARN: exception in %s (total=%llu) - skipping",
                           where, static_cast<unsigned long long>(n));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG SehAbsorbAccessViolation(DWORD code, const char* where, std::atomic<uint64_t>& count) {
    if (code != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    return SehAbsorbAnyException(where, count);
}

} // namespace Fallout4HT
