// SPDX-License-Identifier: MIT
#include "tweaks.h"
#include <shlobj.h>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace fp {

const wchar_t* ToString(TriState t) {
    switch (t) {
        case TriState::On:  return L"On";
        case TriState::Off: return L"Off";
        default:            return L"Unknown";
    }
}

namespace {

// Read a REG_DWORD. Returns Unknown if the value/key is absent.
TriState ReadDwordTri(HKEY root, const wchar_t* sub, const wchar_t* value,
                      DWORD onValue = 1) {
    HKEY key{};
    REGSAM sam = KEY_READ;
    if (RegOpenKeyExW(root, sub, 0, sam, &key) != ERROR_SUCCESS) return TriState::Unknown;
    DWORD data = 0, cb = sizeof(data), type = 0;
    TriState out = TriState::Unknown;
    if (RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)&data, &cb) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        out = (data == onValue) ? TriState::On : TriState::Off;
    }
    RegCloseKey(key);
    return out;
}

// Read a string-encoded DWORD (HAGS HwSchMode is REG_DWORD; some toggles are
// REG_SZ "0"/"1"/"2"). Handles both.
TriState ReadFlexibleTri(HKEY root, const wchar_t* sub, const wchar_t* value, DWORD onValue) {
    HKEY key{};
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return TriState::Unknown;
    DWORD type = 0, cb = 0;
    TriState out = TriState::Unknown;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &cb) == ERROR_SUCCESS) {
        if (type == REG_DWORD) {
            DWORD d = 0; cb = sizeof(d);
            if (RegQueryValueExW(key, value, nullptr, nullptr, (LPBYTE)&d, &cb) == ERROR_SUCCESS)
                out = (d == onValue) ? TriState::On : TriState::Off;
        } else if (type == REG_SZ && cb > 0) {
            std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 1, 0);
            if (RegQueryValueExW(key, value, nullptr, nullptr, (LPBYTE)buf.data(), &cb) == ERROR_SUCCESS) {
                DWORD d = (DWORD)_wtoi(buf.data());
                out = (d == onValue) ? TriState::On : TriState::Off;
            }
        }
    }
    RegCloseKey(key);
    return out;
}

bool WriteDword(HKEY root, const wchar_t* sub, const wchar_t* value, DWORD data) {
    HKEY key{};
    DWORD disp = 0;
    if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disp)
        != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, value, 0, REG_DWORD, (const BYTE*)&data, sizeof(data));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

uint64_t DirSize(const std::wstring& dir, bool& exists) {
    exists = false;
    WIN32_FIND_DATAW fd{};
    std::wstring pattern = dir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    exists = true;
    uint64_t total = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bool sub = false;
            total += DirSize(dir + L"\\" + fd.cFileName, sub);
        } else {
            ULARGE_INTEGER s{};
            s.LowPart = fd.nFileSizeLow; s.HighPart = fd.nFileSizeHigh;
            total += s.QuadPart;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

// Recursively delete directory CONTENTS (keeping the directory itself).
// Returns count of files removed.
long DeleteContents(const std::wstring& dir) {
    long removed = 0;
    WIN32_FIND_DATAW fd{};
    std::wstring pattern = dir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            removed += DeleteContents(full);
            RemoveDirectoryW(full.c_str());
        } else {
            // Clear read-only attr if needed, then delete.
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(full.c_str())) ++removed;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return removed;
}

} // namespace

// --------------------------------------------------------------------------
BrowserHwaState ReadBrowserHwa() {
    BrowserHwaState s;
    s.edge = ReadDwordTri(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Edge", L"HardwareAccelerationModeEnabled");
    s.chrome = ReadDwordTri(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Google\\Chrome", L"HardwareAccelerationModeEnabled");
    // Firefox HWA is controlled via policies.json / about:config, not the
    // registry; report rather than guess.
    s.firefoxPolicyManaged = false;
    s.firefoxNote = L"Firefox: set via policies.json (HardwareAcceleration) "
                    L"or about:config gfx.* — managed deployment recommended.";
    return s;
}

bool SetEdgeHwa(bool enable) {
    return WriteDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Edge",
        L"HardwareAccelerationModeEnabled", enable ? 1u : 0u);
}

bool SetChromeHwa(bool enable) {
    return WriteDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Google\\Chrome",
        L"HardwareAccelerationModeEnabled", enable ? 1u : 0u);
}

// --------------------------------------------------------------------------
CompositorState ReadCompositorState() {
    CompositorState s;
    // HAGS: HwSchMode 2 = On, 1 = Off (per Microsoft / GPU driver docs).
    s.hags = ReadFlexibleTri(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 2);
    // Game Bar / GameDVR.
    s.gameBarEnabled = ReadFlexibleTri(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 1);
    s.backgroundRecording = ReadFlexibleTri(HKEY_CURRENT_USER,
        L"System\\GameConfigStore", L"GameDVR_Enabled", 1);
    s.gameMode = ReadFlexibleTri(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1);
    return s;
}

bool SetHags(bool enable) {
    return WriteDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
        L"HwSchMode", enable ? 2u : 1u);
}

bool SetGameBarEnabled(bool enable) {
    return WriteDword(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
        L"AppCaptureEnabled", enable ? 1u : 0u);
}

bool SetBackgroundRecording(bool enable) {
    return WriteDword(HKEY_CURRENT_USER,
        L"System\\GameConfigStore", L"GameDVR_Enabled", enable ? 1u : 0u);
}

// --------------------------------------------------------------------------
std::vector<ShaderCacheDir> EnumerateShaderCaches() {
    std::vector<ShaderCacheDir> out;
    const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (local.empty()) return out;

    struct Spec { std::wstring rel; std::wstring label; };
    const Spec specs[] = {
        { L"\\NVIDIA\\DXCache",            L"NVIDIA DXCache" },
        { L"\\NVIDIA\\GLCache",            L"NVIDIA GLCache" },
        { L"\\NVIDIA Corporation\\NV_Cache", L"NVIDIA NV_Cache" },
        { L"\\D3DSCache",                  L"D3D Shader Cache" },
        { L"\\AMD\\DxCache",               L"AMD DxCache" },
        { L"\\AMD\\GLCache",               L"AMD GLCache" },
        { L"\\Intel\\ShaderCache",         L"Intel Shader Cache" },
    };
    for (const auto& sp : specs) {
        ShaderCacheDir d;
        d.path = local + sp.rel;
        d.label = sp.label;
        bool exists = false;
        d.sizeBytes = DirSize(d.path, exists);
        d.exists = exists;
        out.push_back(std::move(d));
    }
    return out;
}

long ClearShaderCache(const std::wstring& path) {
    // Safety: only allow paths that EnumerateShaderCaches() would produce.
    bool allowed = false;
    for (const auto& d : EnumerateShaderCaches()) {
        if (_wcsicmp(d.path.c_str(), path.c_str()) == 0) { allowed = true; break; }
    }
    if (!allowed) return -1;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    return DeleteContents(path);
}

} // namespace fp
