// SPDX-License-Identifier: MIT

#include "pch.h"
#include "game_window.h"
#include "core/logging.h"

namespace Fallout4HT {
namespace {

// Anything smaller than this in either dimension is a tool tip or a splash, not
// the game's render window.
constexpr int kMinGameWindowSize = 200;

// How long WatchWindowPlacement waits between polls. Short enough to catch a
// mode change before the player notices, long enough to cost nothing.
constexpr DWORD kPlacementPollMillis = 500;

struct FindWindowContext {
    DWORD pid;
    HWND  hwnd;
};

BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<FindWindowContext*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    if ((r.right - r.left) < kMinGameWindowSize ||
        (r.bottom - r.top) < kMinGameWindowSize) return TRUE;
    ctx->hwnd = hwnd;
    return FALSE;
}

HWND FindGameWindow(int retries) {
    FindWindowContext ctx{ GetCurrentProcessId(), nullptr };
    for (int i = 0; i <= retries; ++i) {
        EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hwnd || i == retries) break;
        Sleep(100);
    }
    return ctx.hwnd;
}

// Centre the window on the monitor it currently sits on. Exclusive fullscreen
// and borderless already fill the screen, so only true windowed mode (which
// carries a caption) is moved.
bool CenterWindowIfWindowed(HWND hwnd) {
    if ((GetWindowLongA(hwnd, GWL_STYLE) & WS_CAPTION) == 0) return false;

    RECT wr;
    if (!GetWindowRect(hwnd, &wr)) return false;
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;

    // NEAREST, not PRIMARY: on a multi-monitor desk the window should centre on
    // the display it is already on rather than jump to the primary one.
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoA(mon, &mi)) return false;

    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    // A window wider or taller than the work area would centre to negative
    // coordinates and put its title bar off-screen, leaving it undraggable.
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    if (wr.left == x && wr.top == y) return false;

    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Log::Line("window: centred windowed mode at %d,%d (size %dx%d)", x, y, w, h);
    return true;
}

// One poll's worth of window geometry, which is what "has the placement
// changed?" is decided from.
struct WindowPlacement {
    HWND hwnd;
    LONG style;
    int width;
    int height;

    bool operator==(const WindowPlacement& other) const {
        return hwnd == other.hwnd && style == other.style
            && width == other.width && height == other.height;
    }
};

} // namespace

void CenterGameWindow() {
    HWND hwnd = FindGameWindow(100);
    if (!hwnd) {
        Log::Line("WARN: CenterGameWindow: game window not found");
        return;
    }

    // Un-minimise BEFORE centring. GetWindowRect on a minimised window reports
    // the restored rect, so centring first decides "already centred" against a
    // rect the window is not currently using.
    //
    // Only un-minimise, nothing more. Forcing Z-order and foreground on a
    // window that is already visible steals focus from whatever the player
    // alt-tabbed to during the load, and on an exclusive-fullscreen swapchain it
    // provokes the game's own fullscreen-state handling into a mode restore.
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
        Log::Line("window: restored from minimised");
    }

    if (!CenterWindowIfWindowed(hwnd)) {
        Log::Line("window: borderless/fullscreen or already centred, leaving position as-is");
    }
}

void WatchWindowPlacement() {
    // WS_MINIMIZE and WS_MAXIMIZE live in GWL_STYLE, so they are masked out:
    // without that, every alt-tab reads as a style change and drags a
    // deliberately-placed window back to the middle.
    constexpr LONG kStateBits = WS_MINIMIZE | WS_MAXIMIZE;

    WindowPlacement acted{};
    WindowPlacement previous{};

    for (;;) {
        Sleep(kPlacementPollMillis);

        HWND hwnd = FindGameWindow(0);
        if (!hwnd) continue;

        RECT wr;
        if (!GetWindowRect(hwnd, &wr)) continue;
        const WindowPlacement current{
            hwnd,
            GetWindowLongA(hwnd, GWL_STYLE) & ~kStateBits,
            static_cast<int>(wr.right - wr.left),
            static_cast<int>(wr.bottom - wr.top),
        };

        // A change only counts once the placement has been the same for two
        // consecutive polls, so dragging a window edge does not get yanked
        // mid-drag. hwnd is deliberately left out of the settle test, matching
        // the original: a brand new window still has to hold still once.
        const bool settled = current.style == previous.style
                          && current.width == previous.width
                          && current.height == previous.height;
        previous = current;
        if (!settled) continue;

        if (current == acted) continue;
        acted = current;

        CenterWindowIfWindowed(hwnd);
    }
}

} // namespace Fallout4HT
