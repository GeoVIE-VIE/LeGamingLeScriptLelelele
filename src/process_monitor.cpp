// SPDX-License-Identifier: MIT
#include "process_monitor.h"
#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "Psapi.lib")

namespace fp {

const std::set<std::wstring>& KnownBrowserImages() {
    static const std::set<std::wstring> kBrowsers = {
        L"firefox.exe", L"chrome.exe", L"msedge.exe", L"brave.exe",
        L"opera.exe", L"opera_gx.exe", L"vivaldi.exe", L"librewolf.exe",
    };
    return kBrowsers;
}

const std::set<std::wstring>& KnownOverlayImages() {
    static const std::set<std::wstring> kOverlays = {
        // Xbox / Windows capture
        L"gamebar.exe", L"gamebarft.exe", L"xboxgamebar.exe", L"gamingservices.exe",
        // Vendor overlays
        L"nvcontainer.exe", L"nvidia overlay.exe", L"nvidia share.exe",
        // Social overlays
        L"discord.exe", L"discordptb.exe", L"discordcanary.exe",
        L"steamoverlayui.exe", L"gameoverlayui.exe",
        // Capture / streaming
        L"obs64.exe", L"obs32.exe", L"rtss.exe", L"rtsshooks64.dll",
        L"medal.exe", L"outplayed.exe", L"streamlabs obs.exe",
    };
    return kOverlays;
}

std::vector<ProcInfo> EnumerateProcesses() {
    std::vector<ProcInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcInfo p;
            p.pid = pe.th32ProcessID;
            p.image = ToLower(pe.szExeFile);
            out.push_back(std::move(p));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

std::wstring ForegroundProcessImage() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return L"";

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring image;
    if (QueryFullProcessImageNameW(h, 0, path, &size)) {
        std::wstring full(path, size);
        size_t slash = full.find_last_of(L"\\/");
        image = ToLower(slash == std::wstring::npos ? full : full.substr(slash + 1));
    }
    CloseHandle(h);
    return image;
}

static std::set<std::wstring> DetectRunningFrom(const std::set<std::wstring>& known) {
    std::set<std::wstring> hits;
    for (const auto& p : EnumerateProcesses()) {
        if (known.count(p.image)) hits.insert(p.image);
    }
    return hits;
}

std::set<std::wstring> DetectRunningBrowsers() { return DetectRunningFrom(KnownBrowserImages()); }
std::set<std::wstring> DetectRunningOverlays() { return DetectRunningFrom(KnownOverlayImages()); }

// ---------------------------------------------------------------------------
ProcessCpuSampler::ProcessCpuSampler(int logicalCores)
    : cores_(logicalCores > 0 ? logicalCores : 1) {}

std::map<std::wstring, double>
ProcessCpuSampler::Sample(const std::set<std::wstring>& imagesOfInterest) {
    std::map<std::wstring, double> result;
    if (imagesOfInterest.empty()) return result;

    const ULONGLONG now = GetTickCount64();
    const double wallMs = (prevWall_ == 0) ? 0.0 : double(now - prevWall_);

    std::map<DWORD, Prev> current;
    // image -> accumulated cpu% this round
    std::map<std::wstring, double> acc;
    for (const auto& img : imagesOfInterest) acc[img] = 0.0;

    for (const auto& p : EnumerateProcesses()) {
        if (!imagesOfInterest.count(p.image)) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
        if (!h) continue;
        FILETIME c{}, e{}, k{}, u{};
        if (GetProcessTimes(h, &c, &e, &k, &u)) {
            ULARGE_INTEGER ku{}, uu{};
            ku.LowPart = k.dwLowDateTime; ku.HighPart = k.dwHighDateTime;
            uu.LowPart = u.dwLowDateTime; uu.HighPart = u.dwHighDateTime;
            Prev cur{ku.QuadPart, uu.QuadPart};
            current[p.pid] = cur;
            auto it = prevByPid_.find(p.pid);
            if (it != prevByPid_.end() && wallMs > 0.0) {
                // 100-ns units delta -> ms; normalize across all cores.
                double busyMs = double((cur.kernel + cur.user) -
                                       (it->second.kernel + it->second.user)) / 10000.0;
                double pct = (busyMs / (wallMs * cores_)) * 100.0;
                if (pct < 0.0) pct = 0.0;
                if (pct > 100.0) pct = 100.0;
                acc[p.image] += pct;
            }
        }
        CloseHandle(h);
    }

    prevByPid_.swap(current);
    prevWall_ = now;
    if (wallMs > 0.0) result = std::move(acc);
    return result;
}

} // namespace fp
