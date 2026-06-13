// SPDX-License-Identifier: MIT
//
// Static hardware inventory gathered once at startup.
//
#pragma once
#include "common.h"
#include <string>

namespace fp {

struct HardwareInfo {
    std::wstring cpuName;
    int          logicalCores = 0;
    int          physicalCores = 0;
    std::wstring gpuName;
    GpuVendor    gpuVendor = GpuVendor::Unknown;
    uint64_t     totalRamBytes = 0;
    std::wstring osVersion;
    int          primaryRefreshHz = 0;
    int          monitorCount = 0;
    bool         multiMonitor = false;
    bool         refreshMismatch = false;   // monitors run at different Hz
};

HardwareInfo GatherHardwareInfo();

} // namespace fp
