// SPDX-License-Identifier: MIT
//
// Frame-time capture via PresentMon. PresentMon is the most universal source
// of CPU/GPU/display frame durations across DirectX, OpenGL, Vulkan, desktop
// and UWP apps, so we shell out to it (read-only) and parse its CSV.
//
// If PresentMon is not bundled / on PATH, capture returns an invalid
// FrameStats with an explanatory note rather than fabricating numbers.
//
#pragma once
#include "common.h"
#include <atomic>
#include <functional>

namespace fp {

// Detect whether a PresentMon binary is reachable.
bool PresentMonAvailable();
std::wstring PresentMonPath();

// Compute frame statistics from a vector of per-frame durations (ms).
FrameStats ComputeFrameStats(std::vector<double> frameTimesMs);

// Run a blocking capture for the given process image over durationSeconds.
// `cancel` is polled so the UI can abort. Returns parsed FrameStats.
FrameStats CaptureFrameTimes(const std::wstring& processImage,
                             int durationSeconds,
                             const std::atomic<bool>& cancel);

} // namespace fp
