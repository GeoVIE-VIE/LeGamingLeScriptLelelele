// SPDX-License-Identifier: MIT
#include "perf_counters.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <vector>

#pragma comment(lib, "Pdh.lib")

namespace fp {

namespace {

PDH_HCOUNTER AddCounter(PDH_HQUERY q, const wchar_t* path) {
    PDH_HCOUNTER c = nullptr;
    if (PdhAddEnglishCounterW(q, path, 0, &c) != ERROR_SUCCESS) {
        // Fall back to localized add for older systems.
        if (PdhAddCounterW(q, path, 0, &c) != ERROR_SUCCESS) return nullptr;
    }
    return c;
}

double FormatDouble(PDH_HCOUNTER c) {
    if (!c) return -1.0;
    PDH_FMT_COUNTERVALUE v{};
    DWORD type = 0;
    if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &type, &v) == ERROR_SUCCESS &&
        v.CStatus == PDH_CSTATUS_VALID_DATA) {
        return v.doubleValue;
    }
    return -1.0;
}

// Sum a wildcard counter (e.g. all GPU engine instances) into one value.
double SumWildcard(PDH_HCOUNTER c) {
    if (!c) return -1.0;
    DWORD bufSize = 0, itemCount = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                 &bufSize, &itemCount, nullptr);
    if (st != PDH_MORE_DATA || bufSize == 0) return -1.0;
    std::vector<uint8_t> buf(bufSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                     &bufSize, &itemCount, items) != ERROR_SUCCESS)
        return -1.0;
    double sum = 0.0;
    bool any = false;
    for (DWORD i = 0; i < itemCount; ++i) {
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
            sum += items[i].FmtValue.doubleValue;
            any = true;
        }
    }
    return any ? sum : -1.0;
}

} // namespace

PerfCounters::PerfCounters() = default;

PerfCounters::~PerfCounters() {
    if (query_) PdhCloseQuery((PDH_HQUERY)query_);
}

bool PerfCounters::Init(int logicalCores) {
    cores_ = logicalCores > 0 ? logicalCores : 1;
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return false;
    query_ = q;

    cpuTotal_  = AddCounter(q, L"\\Processor Information(_Total)\\% Processor Utility");
    if (!cpuTotal_)
        cpuTotal_ = AddCounter(q, L"\\Processor(_Total)\\% Processor Time");
    cpuPerCoreWildcard_ = AddCounter(q, L"\\Processor(*)\\% Processor Time");
    diskActive_ = AddCounter(q, L"\\PhysicalDisk(_Total)\\% Disk Time");
    pageReads_  = AddCounter(q, L"\\Memory\\Page Reads/sec");
    gpu3d_      = AddCounter(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage");
    gpuDecode_  = AddCounter(q, L"\\GPU Engine(*engtype_VideoDecode)\\Utilization Percentage");

    // Prime the query so rate counters have a baseline.
    PdhCollectQueryData(q);
    return true;
}

void PerfCounters::Sample(Snapshot& out) {
    if (!query_) return;
    if (PdhCollectQueryData((PDH_HQUERY)query_) != ERROR_SUCCESS) return;

    double cpu = FormatDouble((PDH_HCOUNTER)cpuTotal_);
    out.cpuTotalPct = cpu < 0 ? 0.0 : (cpu > 100.0 ? 100.0 : cpu);

    // Per-core: PdhGetFormattedCounterArray, skipping the _Total instance.
    out.cpuPerCorePct.clear();
    if (cpuPerCoreWildcard_) {
        DWORD bufSize = 0, count = 0;
        if (PdhGetFormattedCounterArrayW((PDH_HCOUNTER)cpuPerCoreWildcard_,
                PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bufSize, &count, nullptr) == PDH_MORE_DATA &&
            bufSize > 0) {
            std::vector<uint8_t> buf(bufSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
            if (PdhGetFormattedCounterArrayW((PDH_HCOUNTER)cpuPerCoreWildcard_,
                    PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bufSize, &count, items) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < count; ++i) {
                    if (items[i].szName && wcscmp(items[i].szName, L"_Total") == 0) continue;
                    double v = items[i].FmtValue.doubleValue;
                    if (v < 0) v = 0; if (v > 100) v = 100;
                    out.cpuPerCorePct.push_back(v);
                }
            }
        }
    }

    double disk = FormatDouble((PDH_HCOUNTER)diskActive_);
    out.diskActivePct = disk < 0 ? -1.0 : (disk > 100.0 ? 100.0 : disk);

    double pr = FormatDouble((PDH_HCOUNTER)pageReads_);
    out.hardFaultsPerSec = pr < 0 ? 0 : (uint64_t)pr;

    out.gpuUtilPct        = SumWildcard((PDH_HCOUNTER)gpu3d_);
    out.gpuVideoDecodePct = SumWildcard((PDH_HCOUNTER)gpuDecode_);
    if (out.gpuUtilPct > 100.0) out.gpuUtilPct = 100.0;
    if (out.gpuVideoDecodePct > 100.0) out.gpuVideoDecodePct = 100.0;

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) out.memUsedPct = (double)ms.dwMemoryLoad;
}

} // namespace fp
