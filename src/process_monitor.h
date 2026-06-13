// SPDX-License-Identifier: MIT
//
// Process enumeration, foreground detection, per-process CPU sampling, and
// recognition of known browser / overlay-capture processes.
//
#pragma once
#include "common.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fp {

struct ProcInfo {
    DWORD        pid = 0;
    std::wstring image;   // lower-case image name, e.g. "firefox.exe"
};

// Identify the image name (e.g. "cs2.exe") of the current foreground window's
// owning process. Returns empty if it cannot be determined.
std::wstring ForegroundProcessImage();

// Snapshot of every running process (image names lower-cased).
std::vector<ProcInfo> EnumerateProcesses();

// Which well-known browsers are currently running.
std::set<std::wstring> DetectRunningBrowsers();

// Which well-known overlay / capture processes are currently running.
std::set<std::wstring> DetectRunningOverlays();

// Stateful per-process CPU sampler. Call Sample() on a fixed cadence; it
// returns the CPU percentage (normalized to all logical cores = 100%) used by
// every process matching one of the requested image names since the last call.
class ProcessCpuSampler {
public:
    explicit ProcessCpuSampler(int logicalCores);
    // Returns image-name -> CPU% since previous Sample().
    std::map<std::wstring, double> Sample(const std::set<std::wstring>& imagesOfInterest);
private:
    struct Prev { ULONGLONG kernel = 0, user = 0; };
    std::map<DWORD, Prev> prevByPid_;
    ULONGLONG             prevWall_ = 0;
    int                   cores_;
};

// Lists of recognized image names (lower-case). Exposed for the UI / report.
const std::set<std::wstring>& KnownBrowserImages();
const std::set<std::wstring>& KnownOverlayImages();

} // namespace fp
