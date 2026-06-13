// SPDX-License-Identifier: MIT
//
// Renders the human-readable "Frame-Time Interference Report" described in the
// design brief and writes it to disk. The report never says "FPS is good" — it
// states deltas, what is likely, and what to test next.
//
#pragma once
#include "common.h"
#include "system_info.h"
#include "gpu_info.h"
#include "tweaks.h"
#include <string>
#include <vector>

namespace fp {

struct ReportContext {
    HardwareInfo               hw;
    GpuTelemetry               gpu;
    BrowserHwaState            browserHwa;
    CompositorState            compositor;
    std::vector<std::wstring>  runningBrowsers;
    std::vector<std::wstring>  runningOverlays;
    std::vector<ShaderCacheDir> shaderCaches;
    std::vector<RunResult>     runs;
    Diagnosis                  diagnosis;
    bool                       presentMonAvailable = false;
};

// Build the full report as a single wide string.
std::wstring BuildReport(const ReportContext& ctx);

// Write `text` to a UTF-8 file. Returns true on success.
bool WriteTextFileUtf8(const std::wstring& path, const std::wstring& text);

// Suggest a timestamped report path next to the executable (Documents).
std::wstring DefaultReportPath();

} // namespace fp
