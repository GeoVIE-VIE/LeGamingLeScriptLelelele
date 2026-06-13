// SPDX-License-Identifier: MIT
//
// Vendor-specific GPU telemetry. For NVIDIA we query nvidia-smi (read-only)
// for power, clock and memory which PDH does not expose. AMD/Intel fall back
// to PDH utilization only; we report what we can and never guess.
//
#pragma once
#include "common.h"

namespace fp {

struct GpuTelemetry {
    bool   nvidiaSmiPresent = false;
    bool   valid = false;
    double utilPct   = -1.0;
    double powerW    = -1.0;
    double powerCapW = -1.0;
    double clockMhz  = -1.0;
    double memUsedMb = -1.0;
    double memTotalMb= -1.0;
    double tempC     = -1.0;
    std::wstring driverVersion;
    std::wstring name;
};

// Read-only NVIDIA query via nvidia-smi. Returns valid=false on non-NVIDIA
// systems or if the tool is missing.
GpuTelemetry QueryNvidia();

// True if a power value, divided by cap, indicates the GPU is power-limited.
inline bool LooksPowerLimited(const GpuTelemetry& t) {
    return t.valid && t.powerW > 0 && t.powerCapW > 0 && (t.powerW / t.powerCapW) > 0.95;
}

} // namespace fp
