// SPDX-License-Identifier: MIT

#include "pch.h"
#include "camera_snapshot.h"

namespace Fallout4HT {
namespace {

// Published via a seqlock: the writer bumps the sequence to an odd value before
// touching the snapshot and to the next even value after. A reader that observes
// an odd value, or a value that changes across its copy, retries. This protects
// the multi-hundred-byte struct copy from torn reads regardless of whether a
// reader ever runs off the game's main thread.
CameraRootSnapshots g_snapshots = {};
std::atomic<uint32_t> g_snapshotsSeq{0};
uint32_t g_writeSeq = 0;  // writer-private; only the camera hook's thread touches it

// A reader that loses this many races in a row is contending with a writer that
// is not making progress, which cannot happen for a per-frame publish.
constexpr int kMaxReadAttempts = 8;

} // namespace

void PublishCameraRootSnapshots(const CameraRootSnapshots& snapshots) {
    g_snapshotsSeq.store(++g_writeSeq, std::memory_order_release);  // odd: in progress
    // Holds on this target rather than by the standard: MSVC lowers a release
    // fence on x86-64 to a compiler barrier, which blocks reordering in both
    // directions, and x86-64 never reorders store with store. The copy below is
    // formally a data race with any reader, as every seqlock is.
    std::atomic_thread_fence(std::memory_order_release);
    g_snapshots = snapshots;
    g_snapshotsSeq.store(++g_writeSeq, std::memory_order_release);  // even: complete
}

bool GetCameraRootSnapshots(CameraRootSnapshots& out) {
    for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
        const uint32_t seq = g_snapshotsSeq.load(std::memory_order_acquire);
        if (seq == 0) return false;   // nothing has ever been published
        if (seq & 1u) continue;
        out = g_snapshots;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_snapshotsSeq.load(std::memory_order_relaxed) == seq) {
            return out.cameraRoot != 0;  // cameraRoot == 0 is the "invalid frame" marker
        }
        // Sequence moved during the copy - torn read, retry.
    }
    return false;
}

} // namespace Fallout4HT
