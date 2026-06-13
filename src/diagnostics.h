// SPDX-License-Identifier: MIT
//
// The decision tree. Turns a set of matrix runs (plus optional live hardware
// telemetry) into a Diagnosis: primary/secondary likely cause, the measured
// evidence behind it, what is ruled out, and settings to test next.
//
#pragma once
#include "common.h"
#include "system_info.h"
#include <vector>

namespace fp {

// Classify across a full matrix of runs (the richer path: needs >=2 runs to
// compare browser-open vs closed, overlay on/off, etc.).
Diagnosis Diagnose(const std::vector<RunResult>& runs, const HardwareInfo& hw);

// Lightweight single-snapshot classifier for the live dashboard hint, before
// any full run has completed.
IssueClass QuickClassifyLive(const Snapshot& s, const FrameStats* recent);

// Percent change helper exposed for the report ("p99 worse by X%").
double PercentChange(double from, double to);

} // namespace fp
