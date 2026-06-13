// SPDX-License-Identifier: MIT
#include "frametime.h"
#include "subprocess.h"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#pragma comment(lib, "Shlwapi.lib")

namespace fp {

namespace {

// PresentMon ships under a few names depending on version/arch.
const wchar_t* kCandidates[] = {
    L"PresentMon.exe",
    L"PresentMon-2.3.0-x64.exe",
    L"PresentMon-x64.exe",
    L"presentmon.exe",
};

std::wstring TempCsvPath() {
    wchar_t dir[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, dir);
    if (n == 0 || n >= MAX_PATH) return L"";
    wchar_t name[MAX_PATH];
    if (GetTempFileNameW(dir, L"fp_", 0, name) == 0) return L"";
    // Reuse the unique name but with .csv extension.
    std::wstring p(name);
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos) p = p.substr(0, dot);
    return p + L".csv";
}

double Percentile(const std::vector<double>& sorted, double pct) {
    if (sorted.empty()) return 0.0;
    double rank = (pct / 100.0) * (sorted.size() - 1);
    size_t lo = (size_t)std::floor(rank);
    size_t hi = (size_t)std::ceil(rank);
    if (hi >= sorted.size()) hi = sorted.size() - 1;
    double frac = rank - lo;
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

// Locate a frame-time column in a PresentMon CSV header. Different versions
// label it differently; we try the modern and legacy names in order.
int FindFrameTimeColumn(const std::vector<std::string>& header, std::string& apiOut, int& apiCol) {
    apiCol = -1;
    const char* candidates[] = {
        "msbetweenpresents", "frametime", "msbetweendisplaychange", "frametimems",
    };
    int chosen = -1;
    for (size_t i = 0; i < header.size(); ++i) {
        std::string h = header[i];
        std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c){ return (char)tolower(c); });
        if (chosen < 0) {
            for (const char* c : candidates) {
                if (h == c) { chosen = (int)i; break; }
            }
        }
        if (h == "runtime" || h == "presentmode" || h == "api") apiCol = (int)i;
    }
    (void)apiOut;
    return chosen;
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::stringstream ss(line);
    while (std::getline(ss, field, ',')) {
        while (!field.empty() && (field.back() == '\r' || field.back() == ' ')) field.pop_back();
        out.push_back(field);
    }
    return out;
}

} // namespace

std::wstring PresentMonPath() {
    for (const wchar_t* name : kCandidates) {
        std::wstring p = ResolveTool(name);
        if (!p.empty()) return p;
    }
    return L"";
}

bool PresentMonAvailable() { return !PresentMonPath().empty(); }

FrameStats ComputeFrameStats(std::vector<double> ft) {
    FrameStats s;
    // Drop obviously bogus values (negative / absurd) but keep real spikes.
    ft.erase(std::remove_if(ft.begin(), ft.end(),
             [](double v){ return v <= 0.0 || v > 10000.0; }), ft.end());
    if (ft.empty()) {
        s.note = L"No frames captured (target not running or not rendering?).";
        return s;
    }
    s.frames = ft.size();
    double sum = 0.0;
    for (double v : ft) {
        sum += v;
        if (v > s.maxMs) s.maxMs = v;
        if (v > 16.7) ++s.spikes16;
        if (v > 33.3) ++s.spikes33;
        if (v > 50.0) ++s.spikes50;
    }
    s.avgMs = sum / ft.size();
    std::sort(ft.begin(), ft.end());
    s.p95Ms = Percentile(ft, 95.0);
    s.p99Ms = Percentile(ft, 99.0);
    s.valid = true;
    return s;
}

FrameStats CaptureFrameTimes(const std::wstring& processImage,
                             int durationSeconds,
                             const std::atomic<bool>& cancel) {
    FrameStats s;
    std::wstring exe = PresentMonPath();
    if (exe.empty()) {
        s.note = L"PresentMon not found. Place PresentMon.exe in the app's "
                 L"tools\\ folder to enable frame-time capture.";
        return s;
    }
    if (processImage.empty()) {
        s.note = L"No target process specified.";
        return s;
    }

    std::wstring csv = TempCsvPath();
    if (csv.empty()) {
        s.note = L"Could not allocate a temp file for capture output.";
        return s;
    }

    int dur = (std::max)(3, (std::min)(durationSeconds, 600));
    // All arguments are program-controlled; processImage is passed as a single
    // argv element by RunCaptured and cannot break out into a shell.
    std::vector<std::wstring> args = {
        L"--process_name", processImage,
        L"--output_file",  csv,
        L"--timed",        std::to_wstring(dur),
        L"--terminate_after_timed",
        L"--stop_existing_session",
        L"--no_top",
    };

    // Capture runs slightly longer than the timed window; add headroom.
    DWORD timeoutMs = (DWORD)(dur + 10) * 1000u;
    ProcResult r = RunCaptured(exe, args, timeoutMs);
    if (cancel.load()) {
        DeleteFileW(csv.c_str());
        s.note = L"Capture cancelled.";
        return s;
    }
    if (!r.launched) {
        s.note = L"Failed to launch PresentMon (check permissions / antivirus).";
        DeleteFileW(csv.c_str());
        return s;
    }

    // Parse the CSV. MSVC accepts a wide path overload, which is correct for
    // profiles containing non-ASCII characters.
    std::ifstream in(csv.c_str());
    if (!in) {
        s.note = L"PresentMon produced no output. Run as Administrator and "
                 L"ensure the target is actively rendering.";
        DeleteFileW(csv.c_str());
        return s;
    }

    std::string headerLine;
    if (!std::getline(in, headerLine)) {
        in.close();
        DeleteFileW(csv.c_str());
        s.note = L"Empty capture file.";
        return s;
    }
    auto header = SplitCsvLine(headerLine);
    std::string apiName;
    int apiCol = -1;
    int ftCol = FindFrameTimeColumn(header, apiName, apiCol);
    if (ftCol < 0) {
        in.close();
        DeleteFileW(csv.c_str());
        s.note = L"Unrecognized PresentMon CSV format.";
        return s;
    }

    std::vector<double> frames;
    frames.reserve(8192);
    std::string line;
    std::string detectedApi;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto cols = SplitCsvLine(line);
        if ((int)cols.size() <= ftCol) continue;
        try {
            frames.push_back(std::stod(cols[ftCol]));
        } catch (...) { continue; }
        if (apiCol >= 0 && (int)cols.size() > apiCol && detectedApi.empty())
            detectedApi = cols[apiCol];
    }
    in.close();
    DeleteFileW(csv.c_str());

    s = ComputeFrameStats(std::move(frames));
    if (!detectedApi.empty()) s.api = Widen(detectedApi);
    return s;
}

} // namespace fp
