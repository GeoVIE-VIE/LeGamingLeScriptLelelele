// SPDX-License-Identifier: MIT
//
// Engine: owns the worker thread that drives PDH sampling and matrix runs.
// All PDH access happens on this single thread (PDH handles are not shared
// across threads). The UI thread only reads published, mutex-guarded state.
//
#pragma once
#include "common.h"
#include "system_info.h"
#include "gpu_info.h"
#include "perf_counters.h"
#include "process_monitor.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fp {

class Engine {
public:
    Engine();
    ~Engine();

    void Start();          // launch worker thread
    void Stop();           // signal quit and join

    // Queue a matrix run. Thread-safe. Ignored if a run is already active.
    void QueueRun(const RunConfig& cfg);
    void CancelRun();      // request the active run to abort

    // ---- published state (thread-safe getters) ----
    Snapshot       LiveSnapshot() const;
    GpuTelemetry   LiveGpu() const;
    bool           IsRunning() const { return running_.load(); }
    double         RunProgress() const { return progress_.load(); }
    std::wstring   StatusText() const;
    std::vector<RunResult> Runs() const;
    HardwareInfo   Hardware() const { return hw_; }
    void           ClearRuns();

private:
    void WorkerLoop();
    RunResult ExecuteRun(const RunConfig& cfg);
    void SetStatus(const std::wstring& s);

    HardwareInfo  hw_;
    std::thread   worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<double> progress_{0.0};

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<RunConfig> queue_;
    Snapshot      live_;
    GpuTelemetry  gpu_;
    std::wstring  status_;
    std::vector<RunResult> runs_;
};

} // namespace fp
