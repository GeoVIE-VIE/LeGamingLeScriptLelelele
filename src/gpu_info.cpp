// SPDX-License-Identifier: MIT
#include "gpu_info.h"
#include "subprocess.h"
#include <sstream>
#include <vector>

namespace fp {

namespace {

double ParseNum(const std::string& s) {
    try {
        size_t idx = 0;
        // Skip leading spaces.
        size_t b = s.find_first_not_of(" \t");
        if (b == std::string::npos) return -1.0;
        double v = std::stod(s.substr(b), &idx);
        return v;
    } catch (...) {
        return -1.0;
    }
}

std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        size_t b = field.find_first_not_of(" \t\r\n");
        size_t e = field.find_last_not_of(" \t\r\n");
        out.push_back(b == std::string::npos ? "" : field.substr(b, e - b + 1));
    }
    return out;
}

} // namespace

GpuTelemetry QueryNvidia() {
    GpuTelemetry t;
    std::wstring exe = ResolveTool(L"nvidia-smi.exe");
    if (exe.empty()) return t;   // not an NVIDIA system / tool missing
    t.nvidiaSmiPresent = true;

    // Read-only query. Fields are fixed and not user-controlled.
    std::vector<std::wstring> args = {
        L"--query-gpu="
        L"utilization.gpu,power.draw,power.limit,clocks.current.graphics,"
        L"memory.used,memory.total,temperature.gpu,driver_version,name",
        L"--format=csv,noheader,nounits",
    };
    ProcResult r = RunCaptured(exe, args, 8000);
    if (!r.launched || r.stdoutUtf8.empty()) return t;

    // Take the first GPU line.
    std::istringstream lines(r.stdoutUtf8);
    std::string line;
    if (!std::getline(lines, line)) return t;
    auto f = SplitCsv(line);
    if (f.size() < 9) return t;

    t.utilPct    = ParseNum(f[0]);
    t.powerW     = ParseNum(f[1]);
    t.powerCapW  = ParseNum(f[2]);
    t.clockMhz   = ParseNum(f[3]);
    t.memUsedMb  = ParseNum(f[4]);
    t.memTotalMb = ParseNum(f[5]);
    t.tempC      = ParseNum(f[6]);
    t.driverVersion = Widen(f[7]);
    t.name          = Widen(f[8]);
    t.valid = true;
    return t;
}

} // namespace fp
