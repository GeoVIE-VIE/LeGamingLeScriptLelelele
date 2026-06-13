// SPDX-License-Identifier: MIT
//
// System / browser troubleshooting state: reads are always safe; writes are
// guarded and only performed when the UI passes apply=true after explicit
// user confirmation. Every write targets a documented policy / setting key.
//
#pragma once
#include "common.h"
#include <string>
#include <vector>

namespace fp {

// Tri-state for a setting we may or may not be able to read.
enum class TriState { Unknown, Off, On };
const wchar_t* ToString(TriState t);

// ----------------------------- Browser HWA -------------------------------
// Browser hardware-acceleration policy state. Backed by documented enterprise
// policies:
//   Edge:    HKLM\SOFTWARE\Policies\Microsoft\Edge\HardwareAccelerationModeEnabled
//   Chrome:  HKLM\SOFTWARE\Policies\Google\Chrome\HardwareAccelerationModeEnabled
//   Firefox: managed via policies.json (we read/report, see note).
struct BrowserHwaState {
    TriState edge    = TriState::Unknown;
    TriState chrome  = TriState::Unknown;
    bool     firefoxPolicyManaged = false;
    std::wstring firefoxNote;
};

BrowserHwaState ReadBrowserHwa();

// Apply a hardware-acceleration policy for Edge / Chrome (requires admin for
// HKLM). Returns false if the write failed (e.g. not elevated). enable=true
// sets the policy to 1, false to 0.
bool SetEdgeHwa(bool enable);
bool SetChromeHwa(bool enable);

// ----------------------------- HAGS / GameBar ----------------------------
struct CompositorState {
    TriState hags = TriState::Unknown;          // Hardware-Accelerated GPU Scheduling
    TriState gameBarEnabled = TriState::Unknown;
    TriState backgroundRecording = TriState::Unknown;
    TriState gameMode = TriState::Unknown;
};

CompositorState ReadCompositorState();

// HAGS toggle is documented but requires a reboot to take effect; we only
// write the registry value and report that a reboot is needed.
bool SetHags(bool enable);
bool SetGameBarEnabled(bool enable);
bool SetBackgroundRecording(bool enable);

// ----------------------------- Shader cache ------------------------------
struct ShaderCacheDir {
    std::wstring path;
    bool         exists = false;
    uint64_t     sizeBytes = 0;
    std::wstring label;
};

// Enumerate known shader-cache directories with sizes (read-only).
std::vector<ShaderCacheDir> EnumerateShaderCaches();

// Delete the contents of a single known cache directory. For safety the path
// MUST be one returned by EnumerateShaderCaches(); arbitrary paths are
// rejected. Returns number of files removed, or -1 on rejection/error.
long ClearShaderCache(const std::wstring& path);

} // namespace fp
