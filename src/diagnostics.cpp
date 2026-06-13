// SPDX-License-Identifier: MIT
#include "diagnostics.h"
#include <algorithm>
#include <cstdio>

namespace fp {

double PercentChange(double from, double to) {
    if (from <= 0.0) return 0.0;
    return (to - from) / from * 100.0;
}

namespace {

std::wstring Fmt(const wchar_t* fmt, double a) {
    wchar_t buf[256];
    swprintf(buf, 256, fmt, a);
    return buf;
}
std::wstring Fmt2(const wchar_t* fmt, double a, double b) {
    wchar_t buf[256];
    swprintf(buf, 256, fmt, a, b);
    return buf;
}

const RunResult* FindByBrowser(const std::vector<RunResult>& runs, BrowserCondition c) {
    for (const auto& r : runs)
        if (r.config.browser == c && r.frames.valid) return &r;
    return nullptr;
}

const RunResult* FindOverlay(const std::vector<RunResult>& runs, OverlayCondition c) {
    for (const auto& r : runs)
        if (r.config.overlay == c && r.frames.valid) return &r;
    return nullptr;
}

} // namespace

IssueClass QuickClassifyLive(const Snapshot& s, const FrameStats* recent) {
    bool framesBad = recent && recent->valid && recent->p99Ms > 33.3;
    double maxCore = 0.0;
    for (double c : s.cpuPerCorePct) maxCore = (std::max)(maxCore, c);

    if (s.gpuUtilPct >= 0 && s.gpuUtilPct >= 95.0)
        return IssueClass::GpuSaturation;
    if ((s.gpuUtilPct >= 0 && s.gpuUtilPct < 70.0) && maxCore > 90.0 && framesBad)
        return IssueClass::CpuMainThreadStall;
    if (s.browserCpuPct > 15.0 && framesBad)
        return IssueClass::BrowserInterference;
    if (s.hardFaultsPerSec > 1000 && framesBad)
        return IssueClass::CpuMainThreadStall;
    return IssueClass::Inconclusive;
}

Diagnosis Diagnose(const std::vector<RunResult>& runs, const HardwareInfo& hw) {
    Diagnosis d;
    if (runs.empty()) {
        d.evidence.push_back(L"No completed runs yet.");
        return d;
    }

    const RunResult* closed = FindByBrowser(runs, BrowserCondition::Closed);
    const RunResult* open   = nullptr;
    for (auto c : {BrowserCondition::NormalTabs, BrowserCondition::BlankTab,
                   BrowserCondition::VideoPlayback, BrowserCondition::HwAccelOn,
                   BrowserCondition::HwAccelOff}) {
        if (auto* r = FindByBrowser(runs, c)) { open = r; break; }
    }
    const RunResult* hwaOn  = FindByBrowser(runs, BrowserCondition::HwAccelOn);
    const RunResult* hwaOff = FindByBrowser(runs, BrowserCondition::HwAccelOff);
    const RunResult* video  = FindByBrowser(runs, BrowserCondition::VideoPlayback);
    const RunResult* extOff = FindByBrowser(runs, BrowserCondition::ExtensionsDisabled);
    const RunResult* ovlNormal = FindOverlay(runs, OverlayCondition::Normal);
    const RunResult* ovlOff    = FindOverlay(runs, OverlayCondition::Disabled);

    // Pick the "worst" valid run to characterize the GPU/CPU signature.
    const RunResult* worst = nullptr;
    for (const auto& r : runs)
        if (r.frames.valid && (!worst || r.frames.p99Ms > worst->frames.p99Ms))
            worst = &r;

    int score[7] = {0};   // index by (int)IssueClass
    auto add = [&](IssueClass c, int w, const std::wstring& why) {
        score[(int)c] += w;
        if (!why.empty()) d.evidence.push_back(why);
    };

    // ---- Class 1: Browser interference -----------------------------------
    if (closed && open) {
        double delta = PercentChange(closed->frames.p99Ms, open->frames.p99Ms);
        if (delta > 25.0) {
            add(IssueClass::BrowserInterference, 3,
                Fmt2(L"p99 worsened by %.0f%% with browser open (closed %.1f ms vs ",
                     delta, closed->frames.p99Ms) +
                Fmt(L"open %.1f ms).", open->frames.p99Ms));

            if (closed->frames.spikes33 != open->frames.spikes33) {
                d.evidence.push_back(
                    Fmt2(L"Spikes >33.3 ms went from %.0f to %.0f.",
                         (double)closed->frames.spikes33, (double)open->frames.spikes33));
            }
            if (open->browserCpuAvg > 10.0) {
                add(IssueClass::BrowserInterference, 1,
                    Fmt(L"Browser CPU averaged %.0f%% during the open run.",
                        open->browserCpuAvg));
            }
        }
    }
    // Sub-classify HWA direction.
    if (hwaOn && hwaOff) {
        if (hwaOn->frames.p99Ms < hwaOff->frames.p99Ms * 0.8) {
            add(IssueClass::BrowserInterference, 1,
                L"HW accel ON is better -> CPU-side browser rendering/decode load.");
            d.recommendations.push_back(L"Leave browser hardware acceleration ON.");
        } else if (hwaOff->frames.p99Ms < hwaOn->frames.p99Ms * 0.8) {
            add(IssueClass::BrowserInterference, 1,
                L"HW accel OFF is better -> GPU compositor / video-decode / driver path.");
            d.recommendations.push_back(L"Test browser hardware acceleration OFF.");
            d.recommendations.push_back(L"Match refresh rate across monitors; test VRR off for windowed.");
        }
    }
    if (closed && video && PercentChange(closed->frames.p99Ms, video->frames.p99Ms) > 30.0) {
        add(IssueClass::BrowserInterference, 1,
            L"Video playback specifically worsens p99 -> video decode / DWM path.");
        d.recommendations.push_back(L"Pause/close video tabs while gaming.");
    }
    if (open && extOff && extOff->frames.p99Ms < open->frames.p99Ms * 0.8) {
        add(IssueClass::BrowserInterference, 1,
            L"Disabling extensions improves p99 -> an extension is spiking CPU.");
        d.recommendations.push_back(L"Disable heavy extensions / test browser safe mode.");
    }

    // ---- Class 2: GPU saturation / throttle ------------------------------
    if (worst) {
        if (worst->gpuUtilAvg >= 95.0) {
            add(IssueClass::GpuSaturation, 3,
                Fmt(L"GPU utilization averaged %.0f%% in the worst run.", worst->gpuUtilAvg));
            if (worst->gpuPowerAvg >= 90.0)
                add(IssueClass::GpuSaturation, 1,
                    Fmt(L"GPU power averaged %.0f%% of cap (power-limited).", worst->gpuPowerAvg));
            d.recommendations.push_back(L"Lower GPU-bound settings or cap FPS; check temps/power limit.");
        } else if (worst->gpuUtilAvg >= 0 && worst->gpuUtilAvg < 70.0) {
            d.notLikely.push_back(L"GPU saturation (utilization stayed below 70%).");
        }
    }

    // ---- Class 3: CPU / main-thread stall --------------------------------
    if (worst && worst->frames.p99Ms > 25.0) {
        bool gpuLow = worst->gpuUtilAvg >= 0 && worst->gpuUtilAvg < 70.0;
        bool powerLow = worst->gpuPowerAvg >= 0 && worst->gpuPowerAvg < 60.0;
        if (gpuLow && worst->cpuMaxSingle > 90.0) {
            add(IssueClass::CpuMainThreadStall, 3,
                Fmt2(L"GPU low (%.0f%%) but a single CPU core peaked at %.0f%% -> "
                     L"main-thread / contention bound.",
                     worst->gpuUtilAvg, worst->cpuMaxSingle));
            if (powerLow)
                add(IssueClass::CpuMainThreadStall, 1,
                    Fmt(L"GPU power stayed low (%.0f%%).", worst->gpuPowerAvg));
        }
        if (worst->hardFaultsTotal > 5000) {
            add(IssueClass::CpuMainThreadStall, 1,
                Fmt(L"Elevated hard page faults (%.0f total) -> memory pressure / paging.",
                    (double)worst->hardFaultsTotal));
            d.recommendations.push_back(L"Check RAM capacity / XMP-EXPO; close memory-hungry apps.");
        }
    }

    // ---- Class 5: Capture / overlay --------------------------------------
    if (ovlNormal && ovlOff && ovlOff->frames.p99Ms < ovlNormal->frames.p99Ms * 0.8) {
        add(IssueClass::CaptureOverlay, 3,
            Fmt2(L"Disabling overlays improved p99 from %.1f ms to %.1f ms.",
                 ovlNormal->frames.p99Ms, ovlOff->frames.p99Ms));
        d.recommendations.push_back(L"Disable Game Bar background capture and unneeded overlays.");
    }

    // ---- Multi-monitor / refresh hint ------------------------------------
    if (hw.multiMonitor && hw.refreshMismatch) {
        d.evidence.push_back(L"Multiple monitors at mismatched refresh rates "
                             L"(can cause DWM/VRR stutter).");
        d.recommendations.push_back(L"Match monitor refresh rates; test with the second monitor disabled.");
    }

    // ---- Pick primary / secondary ----------------------------------------
    int best = -1, second = -1, bestScore = 0, secondScore = 0;
    for (int i = 1; i < 7; ++i) {
        if (score[i] > bestScore) { second = best; secondScore = bestScore; best = i; bestScore = score[i]; }
        else if (score[i] > secondScore) { second = i; secondScore = score[i]; }
    }
    d.primary   = best   >= 0 && bestScore   > 0 ? (IssueClass)best   : IssueClass::Inconclusive;
    d.secondary = second >= 0 && secondScore > 0 ? (IssueClass)second : IssueClass::Inconclusive;
    int total = 0; for (int i = 1; i < 7; ++i) total += score[i];
    d.confidence = total > 0 ? (double)bestScore / (double)total : 0.0;

    // Always-useful baseline recommendations (deduped).
    std::vector<std::wstring> base = {
        L"Keep NVIDIA Low Latency Mode Off if the app has Reflex (enable Reflex in-game).",
        L"Keep shader cache at Driver Default / 10 GB; clear only if a shader stall is suspected.",
        L"Use an in-game FPS cap ladder (monitor Hz, 75% Hz, then a fixed target).",
        L"Test on stock GPU clocks (no OC/undervolt) and a stable XMP/EXPO profile.",
    };
    for (auto& b : base) {
        if (std::find(d.recommendations.begin(), d.recommendations.end(), b) == d.recommendations.end())
            d.recommendations.push_back(b);
    }

    if (d.primary == IssueClass::Inconclusive)
        d.evidence.push_back(L"No single area dominated; gather more matrix runs "
                             L"(closed vs open, overlay on/off, HWA on/off).");
    return d;
}

} // namespace fp
