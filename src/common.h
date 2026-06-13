// SPDX-License-Identifier: MIT
//
// FrameProbe - Windows Frame-Time Interference Diagnostic
//
// Shared data model and small helpers used across every module.
// Everything here is plain value types so modules stay decoupled and
// easy to unit test on their own.
//
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace fp {

// ---------------------------------------------------------------------------
// Branding. Kept in one place so the watermark and report header stay in sync.
// ---------------------------------------------------------------------------
constexpr wchar_t kAppName[]   = L"FrameProbe";
constexpr wchar_t kAppTagline[] = L"Windows Frame-Time Interference Diagnostic";
constexpr wchar_t kWatermark[] = L"geovie";
constexpr wchar_t kVersion[]   = L"1.0.0";

// ---------------------------------------------------------------------------
// Configuration captured from the user before a test run. Mirrors the
// "Target process / Test duration / Browser condition / ..." block in the
// design brief.
// ---------------------------------------------------------------------------
enum class BrowserCondition {
    Closed,
    BlankTab,
    NormalTabs,
    VideoPlayback,
    Minimized,
    ExtensionsDisabled,
    HwAccelOn,
    HwAccelOff,
};

enum class OverlayCondition {
    Normal,
    Disabled,
    BackgroundRecordingOff,
};

enum class DisplayMode {
    Unknown,
    FullscreenExclusive,
    Borderless,
    Windowed,
};

enum class GpuVendor {
    Unknown,
    Nvidia,
    Amd,
    Intel,
};

struct RunConfig {
    std::wstring     targetProcess;          // e.g. "cs2.exe" (image name only)
    int              durationSeconds = 30;   // capture window length
    BrowserCondition browser  = BrowserCondition::Closed;
    OverlayCondition overlay  = OverlayCondition::Normal;
    GpuVendor        gpu      = GpuVendor::Unknown;
    DisplayMode      display  = DisplayMode::Unknown;
    int              refreshHz = 0;          // 0 == auto-detect
    std::wstring     label;                  // friendly run label for the matrix
};

// ---------------------------------------------------------------------------
// Live sampled metrics. A snapshot is produced roughly every sample interval
// by the perf-counter collector; runs aggregate many snapshots.
// ---------------------------------------------------------------------------
struct Snapshot {
    double cpuTotalPct       = 0.0;
    std::vector<double> cpuPerCorePct;
    double targetCpuPct      = 0.0;   // target process CPU (% of one core normalized)
    double browserCpuPct     = 0.0;   // sum across detected browser processes
    double gpuUtilPct        = -1.0;  // -1 == unavailable
    double gpuPowerPct       = -1.0;
    double gpuClockMhz       = -1.0;
    double gpuMemUsedMb      = -1.0;
    double gpuVideoDecodePct = -1.0;
    double diskActivePct     = -1.0;
    double memUsedPct        = 0.0;
    uint64_t hardFaultsPerSec = 0;
};

// ---------------------------------------------------------------------------
// Frame-time statistics for one run (from PresentMon, when available).
// ---------------------------------------------------------------------------
struct FrameStats {
    bool   valid       = false;
    double avgMs       = 0.0;
    double p95Ms       = 0.0;
    double p99Ms       = 0.0;
    double maxMs       = 0.0;
    uint64_t frames    = 0;
    uint64_t spikes16  = 0;   // frames over 16.7 ms
    uint64_t spikes33  = 0;   // frames over 33.3 ms
    uint64_t spikes50  = 0;   // frames over 50 ms
    std::wstring api;         // "DirectX 12" / "Vulkan" / etc. if reported
    std::wstring note;        // human note: why invalid, source used, etc.
};

// ---------------------------------------------------------------------------
// Aggregated result for a single matrix run.
// ---------------------------------------------------------------------------
struct RunResult {
    RunConfig  config;
    FrameStats frames;
    // Aggregated snapshot stats over the run window.
    double cpuTotalAvg     = 0.0;
    double cpuMaxSingle    = 0.0;   // peak of any single core
    double targetCpuAvg    = 0.0;
    double browserCpuAvg   = 0.0;
    double gpuUtilAvg      = -1.0;
    double gpuPowerAvg     = -1.0;
    double gpuClockMin     = -1.0;
    double gpuMemUsedMaxMb = -1.0;
    double gpuVideoDecodeAvg = -1.0;
    double diskActiveAvg   = -1.0;
    double memUsedAvg      = 0.0;
    uint64_t hardFaultsTotal = 0;
    SYSTEMTIME when{};
};

// ---------------------------------------------------------------------------
// Diagnostic classification result (the decision tree output).
// ---------------------------------------------------------------------------
enum class IssueClass {
    Inconclusive,
    BrowserInterference,
    GpuSaturation,
    CpuMainThreadStall,
    ShaderCacheStall,
    CaptureOverlay,
    NetworkJitter,
};

struct Diagnosis {
    IssueClass primary   = IssueClass::Inconclusive;
    IssueClass secondary = IssueClass::Inconclusive;
    std::vector<std::wstring> evidence;       // bullet points of measured facts
    std::vector<std::wstring> notLikely;      // ruled-out areas
    std::vector<std::wstring> recommendations;
    double confidence = 0.0;                  // 0..1 heuristic confidence
};

const wchar_t* ToString(IssueClass c);
const wchar_t* ToString(GpuVendor v);
const wchar_t* ToString(BrowserCondition c);
const wchar_t* ToString(OverlayCondition c);
const wchar_t* ToString(DisplayMode m);

// ---------------------------------------------------------------------------
// Small string helpers.
// ---------------------------------------------------------------------------
std::wstring Trim(const std::wstring& s);
std::wstring ToLower(std::wstring s);
std::string  Narrow(const std::wstring& w);   // UTF-16 -> UTF-8
std::wstring Widen(const std::string& s);     // UTF-8  -> UTF-16

} // namespace fp
