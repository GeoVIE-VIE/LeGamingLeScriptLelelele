// SPDX-License-Identifier: MIT
#include "common.h"

namespace fp {

const wchar_t* ToString(IssueClass c) {
    switch (c) {
        case IssueClass::BrowserInterference: return L"Browser interference";
        case IssueClass::GpuSaturation:       return L"GPU saturation / throttle";
        case IssueClass::CpuMainThreadStall:  return L"CPU / main-thread stall";
        case IssueClass::ShaderCacheStall:    return L"Shader / cache stall";
        case IssueClass::CaptureOverlay:      return L"Capture / overlay hook";
        case IssueClass::NetworkJitter:       return L"Network jitter (not frame-time)";
        case IssueClass::Inconclusive:        return L"Inconclusive";
    }
    return L"Inconclusive";
}

const wchar_t* ToString(GpuVendor v) {
    switch (v) {
        case GpuVendor::Nvidia: return L"NVIDIA";
        case GpuVendor::Amd:    return L"AMD";
        case GpuVendor::Intel:  return L"Intel";
        case GpuVendor::Unknown:return L"Unknown";
    }
    return L"Unknown";
}

const wchar_t* ToString(BrowserCondition c) {
    switch (c) {
        case BrowserCondition::Closed:             return L"Browser closed";
        case BrowserCondition::BlankTab:           return L"Browser open, blank tab";
        case BrowserCondition::NormalTabs:         return L"Browser open, normal tabs";
        case BrowserCondition::VideoPlayback:      return L"Browser open, video playing";
        case BrowserCondition::Minimized:          return L"Browser open, minimized";
        case BrowserCondition::ExtensionsDisabled: return L"Browser extensions disabled";
        case BrowserCondition::HwAccelOn:          return L"Browser HW accel ON";
        case BrowserCondition::HwAccelOff:         return L"Browser HW accel OFF";
    }
    return L"Browser closed";
}

const wchar_t* ToString(OverlayCondition c) {
    switch (c) {
        case OverlayCondition::Normal:                 return L"Overlays normal";
        case OverlayCondition::Disabled:               return L"Overlays disabled";
        case OverlayCondition::BackgroundRecordingOff: return L"Background recording off";
    }
    return L"Overlays normal";
}

const wchar_t* ToString(DisplayMode m) {
    switch (m) {
        case DisplayMode::FullscreenExclusive: return L"Fullscreen exclusive";
        case DisplayMode::Borderless:          return L"Borderless";
        case DisplayMode::Windowed:            return L"Windowed";
        case DisplayMode::Unknown:             return L"Unknown";
    }
    return L"Unknown";
}

std::wstring Trim(const std::wstring& s) {
    const wchar_t* ws = L" \t\r\n\"";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::wstring::npos) return L"";
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::wstring ToLower(std::wstring s) {
    for (auto& ch : s) ch = (wchar_t)towlower(ch);
    return s;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

} // namespace fp
