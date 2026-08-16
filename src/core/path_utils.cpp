// SPDX-License-Identifier: MIT

#include "pch.h"
#include "path_utils.h"

namespace Fallout4HT {
namespace {

// Only its address matters: it is what identifies OUR module to
// GetModuleHandleEx, rather than the game's.
void DummyAddress() {}

std::string GetModuleDirectory() {
    HMODULE hModule = nullptr;

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&DummyAddress),
            &hModule) || hModule == nullptr) {
        return "";
    }

    // Grow buffer until the path fits. GetModuleFileNameA truncates and
    // returns nSize without setting an error on Windows < 10 1607, and sets
    // ERROR_INSUFFICIENT_BUFFER + returns nSize on newer Windows. Either way,
    // a return value equal to the buffer size means the path was truncated;
    // a deep install path otherwise silently writes the log/config to the
    // wrong directory.
    DWORD bufSize = MAX_PATH;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::vector<char> buf(bufSize);
        SetLastError(0);
        DWORD ret = GetModuleFileNameA(hModule, buf.data(), bufSize);
        if (ret == 0) return "";
        if (ret < bufSize && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            std::string path(buf.data(), ret);
            size_t lastSlash = path.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                return path.substr(0, lastSlash + 1);
            }
            return "";
        }
        bufSize *= 2;
    }
    return "";
}

} // namespace

std::string GetModulePath(const char* filename) {
    std::string dir = GetModuleDirectory();
    if (dir.empty()) {
        // Return empty so callers can detect failure rather than silently
        // writing to the game's CWD when the module path lookup fails.
        return "";
    }
    return dir + filename;
}

std::wstring GetModulePathW(const char* filename) {
    std::string narrow = GetModulePath(filename);
    if (narrow.empty()) return {};

    int len = MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, &wide[0], len);
    wide.pop_back();
    return wide;
}

} // namespace Fallout4HT
