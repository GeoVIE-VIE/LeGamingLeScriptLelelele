// SPDX-License-Identifier: MIT
#include "engine.h"
#include "frametime.h"
#include <algorithm>
#include <chrono>

namespace fp {

using namespace std::chrono_literals;

Engine::Engine() {
    hw_ = GatherHardwareInfo();
}

Engine::~Engine() { Stop(); }

void Engine::Start() {
    if (worker_.joinable()) return;
    quit_.store(false);
    worker_ = std::thread([this] { WorkerLoop(); });
}

void Engine::Stop() {
    quit_.store(true);
    cancel_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Engine::QueueRun(const RunConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.push_back(cfg);
    }
    cv_.notify_all();
}

void Engine::CancelRun() { cancel_.store(true); }

Snapshot Engine::LiveSnapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return live_;
}

GpuTelemetry Engine::LiveGpu() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return gpu_;
}

std::wstring Engine::StatusText() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return status_;
}

std::vector<RunResult> Engine::Runs() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return runs_;
}

void Engine::ClearRuns() {
    std::lock_guard<std::mutex> lk(mtx_);
    runs_.clear();
}

void Engine::SetStatus(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(mtx_);
    status_ = s;
}

void Engine::WorkerLoop() {
    PerfCounters perf;
    perf.Init(hw_.logicalCores);
    ProcessCpuSampler procCpu(hw_.logicalCores);

    SetStatus(L"Idle - monitoring");
    auto lastGpu = std::chrono::steady_clock::now() - 5s;

    while (!quit_.load()) {
        // Pull a queued run, if any.
        RunConfig cfg;
        bool haveRun = false;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!queue_.empty()) {
                cfg = queue_.front();
                queue_.pop_front();
                haveRun = true;
            }
        }

        if (haveRun) {
            cancel_.store(false);
            running_.store(true);
            RunResult rr = ExecuteRun(cfg);
            running_.store(false);
            progress_.store(0.0);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (!cancel_.load()) runs_.push_back(rr);
            }
            SetStatus(cancel_.load() ? L"Run cancelled" : L"Run complete");
            continue;
        }

        // Idle live sampling tick.
        Snapshot s;
        perf.Sample(s);
        auto procUsage = procCpu.Sample([&] {
            std::set<std::wstring> imgs = KnownBrowserImages();
            if (!cfg.targetProcess.empty()) imgs.insert(ToLower(cfg.targetProcess));
            return imgs;
        }());
        double browserCpu = 0.0;
        for (const auto& kv : procUsage)
            if (KnownBrowserImages().count(kv.first)) browserCpu += kv.second;
        s.browserCpuPct = browserCpu;

        // Refresh NVIDIA telemetry less often (it spawns a process).
        auto nowt = std::chrono::steady_clock::now();
        GpuTelemetry g;
        bool gotGpu = false;
        if (nowt - lastGpu > 3s) {
            g = QueryNvidia();
            lastGpu = nowt;
            gotGpu = true;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            live_ = s;
            if (gotGpu) gpu_ = g;
            if (gpu_.valid) {
                live_.gpuPowerPct = (gpu_.powerCapW > 0)
                    ? (gpu_.powerW / gpu_.powerCapW * 100.0) : -1.0;
                live_.gpuClockMhz = gpu_.clockMhz;
                live_.gpuMemUsedMb = gpu_.memUsedMb;
                if (live_.gpuUtilPct < 0) live_.gpuUtilPct = gpu_.utilPct;
            }
        }

        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, 350ms, [this] { return quit_.load() || !queue_.empty(); });
    }
}

RunResult Engine::ExecuteRun(const RunConfig& cfg) {
    RunResult rr;
    rr.config = cfg;
    GetLocalTime(&rr.when);

    const std::wstring target = ToLower(cfg.targetProcess);

    SetStatus(L"Capturing frame-times for " + cfg.targetProcess + L" ...");

    // Frame capture runs on its own thread for the full duration; meanwhile we
    // keep sampling perf counters on this thread to characterize the window.
    std::atomic<bool> capDone{false};
    FrameStats frames;
    std::thread capThread([&] {
        frames = CaptureFrameTimes(target, cfg.durationSeconds, cancel_);
        capDone.store(true);
    });

    PerfCounters perf;
    perf.Init(hw_.logicalCores);
    ProcessCpuSampler procCpu(hw_.logicalCores);

    std::set<std::wstring> imgs = KnownBrowserImages();
    if (!target.empty()) imgs.insert(target);

    // Aggregation accumulators.
    int samples = 0;
    double cpuTotalSum = 0, targetSum = 0, browserSum = 0;
    double cpuMaxSingle = 0;
    double gpuUtilSum = 0, gpuPowerSum = 0, gpuDecodeSum = 0, diskSum = 0, memSum = 0;
    int gpuUtilN = 0, gpuPowerN = 0, gpuDecodeN = 0, diskN = 0;
    double gpuClockMin = -1, gpuMemMax = -1;
    uint64_t hardFaults = 0;

    const auto start = std::chrono::steady_clock::now();
    const auto total = std::chrono::seconds((std::max)(3, cfg.durationSeconds));
    auto lastGpu = start - 5s;
    GpuTelemetry g;

    while (!capDone.load() && !cancel_.load()) {
        Snapshot s;
        perf.Sample(s);
        auto usage = procCpu.Sample(imgs);
        double tcpu = 0, bcpu = 0;
        for (const auto& kv : usage) {
            if (kv.first == target) tcpu += kv.second;
            if (KnownBrowserImages().count(kv.first)) bcpu += kv.second;
        }

        auto nowt = std::chrono::steady_clock::now();
        if (nowt - lastGpu > 2s) { g = QueryNvidia(); lastGpu = nowt; }

        if (samples > 0) {  // skip the first (priming) sample
            cpuTotalSum += s.cpuTotalPct;
            targetSum += tcpu;
            browserSum += bcpu;
            for (double c : s.cpuPerCorePct) cpuMaxSingle = (std::max)(cpuMaxSingle, c);
            if (s.gpuUtilPct >= 0) { gpuUtilSum += s.gpuUtilPct; ++gpuUtilN; }
            else if (g.valid)      { gpuUtilSum += g.utilPct; ++gpuUtilN; }
            if (s.gpuVideoDecodePct >= 0) { gpuDecodeSum += s.gpuVideoDecodePct; ++gpuDecodeN; }
            if (s.diskActivePct >= 0) { diskSum += s.diskActivePct; ++diskN; }
            memSum += s.memUsedPct;
            hardFaults += s.hardFaultsPerSec;  // per-sample approx (350ms cadence)
            if (g.valid) {
                if (g.powerCapW > 0) { gpuPowerSum += (g.powerW / g.powerCapW * 100.0); ++gpuPowerN; }
                if (g.clockMhz >= 0) gpuClockMin = (gpuClockMin < 0) ? g.clockMhz
                                                    : (std::min)(gpuClockMin, g.clockMhz);
                if (g.memUsedMb >= 0) gpuMemMax = (std::max)(gpuMemMax, g.memUsedMb);
            }
        }
        ++samples;

        // publish a live snapshot too
        {
            std::lock_guard<std::mutex> lk(mtx_);
            live_ = s;
            live_.targetCpuPct = tcpu;
            live_.browserCpuPct = bcpu;
            if (g.valid) gpu_ = g;
        }

        double elapsed = std::chrono::duration<double>(nowt - start).count();
        progress_.store((std::min)(1.0, elapsed / (double)total.count()));
        std::this_thread::sleep_for(350ms);
    }

    if (capThread.joinable()) capThread.join();

    rr.frames = frames;
    int n = (std::max)(1, samples - 1);
    rr.cpuTotalAvg   = cpuTotalSum / n;
    rr.targetCpuAvg  = targetSum / n;
    rr.browserCpuAvg = browserSum / n;
    rr.cpuMaxSingle  = cpuMaxSingle;
    rr.gpuUtilAvg    = gpuUtilN ? gpuUtilSum / gpuUtilN : -1.0;
    rr.gpuPowerAvg   = gpuPowerN ? gpuPowerSum / gpuPowerN : -1.0;
    rr.gpuVideoDecodeAvg = gpuDecodeN ? gpuDecodeSum / gpuDecodeN : -1.0;
    rr.diskActiveAvg = diskN ? diskSum / diskN : -1.0;
    rr.memUsedAvg    = memSum / n;
    rr.gpuClockMin   = gpuClockMin;
    rr.gpuMemUsedMaxMb = gpuMemMax;
    rr.hardFaultsTotal = hardFaults;
    return rr;
}

} // namespace fp
