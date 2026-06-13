// SPDX-License-Identifier: MIT
//
// PDH (Performance Data Helper) based live counters: total + per-core CPU,
// disk active time, memory page reads (hard faults), and GPU engine
// utilization (3D + video decode) summed across adapters.
//
#pragma once
#include "common.h"
#include <vector>

namespace fp {

class PerfCounters {
public:
    PerfCounters();
    ~PerfCounters();
    PerfCounters(const PerfCounters&) = delete;
    PerfCounters& operator=(const PerfCounters&) = delete;

    bool Init(int logicalCores);

    // Collect one sample. PDH needs two collections spaced in time for rate
    // counters, so the first call after Init() returns mostly zeros.
    void Sample(Snapshot& out);

    bool Available() const { return query_ != nullptr; }

private:
    void* query_ = nullptr;             // PDH_HQUERY
    void* cpuTotal_ = nullptr;          // PDH_HCOUNTER
    std::vector<void*> cpuPerCore_;     // one wildcard counter, expanded
    void* cpuPerCoreWildcard_ = nullptr;
    void* diskActive_ = nullptr;
    void* pageReads_ = nullptr;
    void* gpu3d_ = nullptr;             // wildcard GPU Engine 3D
    void* gpuDecode_ = nullptr;         // wildcard GPU Engine VideoDecode
    int   cores_ = 0;
};

} // namespace fp
