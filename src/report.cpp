// SPDX-License-Identifier: MIT
#include "report.h"
#include "diagnostics.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace fp {

namespace {

std::wstring F1(double v) {
    if (v < 0) return L"n/a";
    std::wostringstream o; o << std::fixed << std::setprecision(1) << v; return o.str();
}
std::wstring F0(double v) {
    if (v < 0) return L"n/a";
    std::wostringstream o; o << std::fixed << std::setprecision(0) << v; return o.str();
}
std::wstring Mb(uint64_t bytes) {
    std::wostringstream o; o << (bytes / (1024 * 1024)) << L" MB"; return o.str();
}
std::wstring Gb(uint64_t bytes) {
    std::wostringstream o; o << std::fixed << std::setprecision(1)
        << (double)bytes / (1024.0 * 1024.0 * 1024.0) << L" GB"; return o.str();
}

void Line(std::wstring& s, const std::wstring& l) { s += l; s += L"\r\n"; }

} // namespace

std::wstring BuildReport(const ReportContext& ctx) {
    std::wstring s;
    SYSTEMTIME now; GetLocalTime(&now);
    wchar_t ts[64];
    swprintf(ts, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    Line(s, std::wstring(kAppName) + L" - " + kAppTagline);
    Line(s, std::wstring(L"Generated: ") + ts + L"   (build " + kVersion + L", by " + kWatermark + L")");
    Line(s, L"========================================================================");
    Line(s, L"");

    // -- Target / system --
    Line(s, L"TARGET & SYSTEM");
    Line(s, L"------------------------------------------------------------------------");
    std::wstring target = ctx.runs.empty() ? L"(not set)" : ctx.runs.front().config.targetProcess;
    Line(s, L"Target process : " + (target.empty() ? L"(not set)" : target));
    Line(s, L"CPU            : " + ctx.hw.cpuName);
    {
        std::wostringstream o; o << ctx.hw.physicalCores << L" cores / "
                                 << ctx.hw.logicalCores << L" threads";
        Line(s, L"Cores          : " + o.str());
    }
    Line(s, L"GPU            : " + ctx.hw.gpuName + L"  (" + ToString(ctx.hw.gpuVendor) + L")");
    if (ctx.gpu.valid)
        Line(s, L"GPU driver     : " + ctx.gpu.driverVersion);
    Line(s, L"RAM            : " + Gb(ctx.hw.totalRamBytes));
    Line(s, L"OS             : " + ctx.hw.osVersion);
    {
        std::wostringstream o; o << ctx.hw.monitorCount << L" monitor(s), primary "
                                 << ctx.hw.primaryRefreshHz << L" Hz"
                                 << (ctx.hw.refreshMismatch ? L"  [MIXED REFRESH RATES]" : L"");
        Line(s, L"Displays       : " + o.str());
    }
    Line(s, L"Frame capture  : " + std::wstring(ctx.presentMonAvailable
            ? L"PresentMon detected" : L"PresentMon NOT found (frame stats unavailable)"));
    Line(s, L"");

    // -- Environment state --
    Line(s, L"ENVIRONMENT STATE");
    Line(s, L"------------------------------------------------------------------------");
    {
        std::wstring b;
        for (auto& x : ctx.runningBrowsers) { if (!b.empty()) b += L", "; b += x; }
        Line(s, L"Browsers open  : " + (b.empty() ? L"none" : b));
        std::wstring o;
        for (auto& x : ctx.runningOverlays) { if (!o.empty()) o += L", "; o += x; }
        Line(s, L"Overlays/cap   : " + (o.empty() ? L"none detected" : o));
    }
    Line(s, std::wstring(L"Edge HWA policy   : ") + ToString(ctx.browserHwa.edge));
    Line(s, std::wstring(L"Chrome HWA policy : ") + ToString(ctx.browserHwa.chrome));
    Line(s, std::wstring(L"HAGS              : ") + ToString(ctx.compositor.hags));
    Line(s, std::wstring(L"Game Bar capture  : ") + ToString(ctx.compositor.gameBarEnabled));
    Line(s, std::wstring(L"Background record  : ") + ToString(ctx.compositor.backgroundRecording));
    Line(s, std::wstring(L"Game Mode         : ") + ToString(ctx.compositor.gameMode));
    Line(s, L"");

    // -- Live GPU telemetry --
    if (ctx.gpu.valid) {
        Line(s, L"GPU TELEMETRY (nvidia-smi)");
        Line(s, L"------------------------------------------------------------------------");
        Line(s, L"Utilization : " + F0(ctx.gpu.utilPct) + L" %");
        Line(s, L"Power       : " + F0(ctx.gpu.powerW) + L" W / " + F0(ctx.gpu.powerCapW) + L" W cap");
        Line(s, L"Core clock  : " + F0(ctx.gpu.clockMhz) + L" MHz");
        Line(s, L"Memory      : " + F0(ctx.gpu.memUsedMb) + L" / " + F0(ctx.gpu.memTotalMb) + L" MB");
        Line(s, L"Temp        : " + F0(ctx.gpu.tempC) + L" C");
        Line(s, L"");
    }

    // -- Matrix runs --
    Line(s, L"TEST MATRIX RESULTS");
    Line(s, L"------------------------------------------------------------------------");
    if (ctx.runs.empty()) {
        Line(s, L"(no runs recorded)");
    } else {
        for (size_t i = 0; i < ctx.runs.size(); ++i) {
            const auto& r = ctx.runs[i];
            std::wstring label = r.config.label.empty()
                ? (std::wstring(ToString(r.config.browser)) + L", " + ToString(r.config.overlay))
                : r.config.label;
            std::wostringstream head; head << L"Run " << (i + 1) << L": " << label;
            Line(s, head.str());
            if (r.frames.valid) {
                Line(s, L"   frametime avg " + F1(r.frames.avgMs) + L" ms | p95 " +
                        F1(r.frames.p95Ms) + L" ms | p99 " + F1(r.frames.p99Ms) +
                        L" ms | max " + F1(r.frames.maxMs) + L" ms");
                std::wostringstream sp;
                sp << L"   spikes >16.7ms: " << r.frames.spikes16
                   << L" | >33.3ms: " << r.frames.spikes33
                   << L" | >50ms: " << r.frames.spikes50
                   << L" (" << r.frames.frames << L" frames)";
                Line(s, sp.str());
            } else {
                Line(s, L"   frametime: " + (r.frames.note.empty() ? L"unavailable" : r.frames.note));
            }
            Line(s, L"   CPU total avg " + F0(r.cpuTotalAvg) + L"% | max single-core " +
                    F0(r.cpuMaxSingle) + L"% | target " + F0(r.targetCpuAvg) +
                    L"% | browser " + F0(r.browserCpuAvg) + L"%");
            Line(s, L"   GPU util avg " + F0(r.gpuUtilAvg) + L"% | power avg " +
                    F0(r.gpuPowerAvg) + L"% | clock min " + F0(r.gpuClockMin) +
                    L" MHz | vram max " + F0(r.gpuMemUsedMaxMb) + L" MB");
            Line(s, L"   disk active avg " + F0(r.diskActiveAvg) + L"% | mem used avg " +
                    F0(r.memUsedAvg) + L"% | hard faults " + std::to_wstring(r.hardFaultsTotal));
            Line(s, L"");
        }
    }

    // -- Shader caches --
    Line(s, L"SHADER CACHES (read-only inventory)");
    Line(s, L"------------------------------------------------------------------------");
    if (ctx.shaderCaches.empty()) {
        Line(s, L"(none found)");
    } else {
        for (const auto& c : ctx.shaderCaches) {
            if (!c.exists) continue;
            Line(s, L"   " + c.label + L"  (" + Mb(c.sizeBytes) + L")  " + c.path);
        }
    }
    Line(s, L"Note: do NOT score the first run after clearing a shader cache "
            L"(shaders rebuild). Score the 2nd/3rd warmed run.");
    Line(s, L"");

    // -- Classification --
    Line(s, L"CLASSIFICATION");
    Line(s, L"------------------------------------------------------------------------");
    {
        std::wostringstream o;
        o << L"Primary likely issue : " << ToString(ctx.diagnosis.primary)
          << L"  (confidence " << std::fixed << std::setprecision(0)
          << (ctx.diagnosis.confidence * 100.0) << L"%)";
        Line(s, o.str());
    }
    Line(s, std::wstring(L"Secondary            : ") + ToString(ctx.diagnosis.secondary));
    Line(s, L"");
    Line(s, L"Evidence:");
    for (const auto& e : ctx.diagnosis.evidence) Line(s, L"   - " + e);
    if (!ctx.diagnosis.notLikely.empty()) {
        Line(s, L"Not likely:");
        for (const auto& e : ctx.diagnosis.notLikely) Line(s, L"   - " + e);
    }
    Line(s, L"");
    Line(s, L"Recommended settings to test (not permanent changes):");
    int n = 1;
    for (const auto& r : ctx.diagnosis.recommendations) {
        std::wostringstream o; o << L"   " << n++ << L". " << r;
        Line(s, o.str());
    }
    Line(s, L"");
    Line(s, L"========================================================================");
    Line(s, std::wstring(L"  ") + kWatermark + L" - " + kAppName + L" " + kVersion);
    return s;
}

bool WriteTextFileUtf8(const std::wstring& path, const std::wstring& text) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    // UTF-8 BOM so Notepad renders it correctly.
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    f.write(reinterpret_cast<const char*>(bom), 3);
    std::string utf8 = Narrow(text);
    f.write(utf8.data(), (std::streamsize)utf8.size());
    return f.good();
}

std::wstring DefaultReportPath() {
    PWSTR docs = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
        dir = docs;
    if (docs) CoTaskMemFree(docs);
    if (dir.empty()) dir = L".";
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t name[128];
    swprintf(name, 128, L"\\FrameProbe_Report_%04d%02d%02d_%02d%02d%02d.txt",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return dir + name;
}

} // namespace fp
