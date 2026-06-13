// SPDX-License-Identifier: MIT
#include "system_info.h"
#include <vector>

namespace fp {

namespace {

std::wstring ReadRegString(HKEY root, const wchar_t* sub, const wchar_t* value) {
    HKEY key{};
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return L"";
    wchar_t buf[512];
    DWORD cb = sizeof(buf);
    DWORD type = 0;
    std::wstring out;
    if (RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        out.assign(buf, cb / sizeof(wchar_t));
        if (!out.empty() && out.back() == L'\0') out.pop_back();
    }
    RegCloseKey(key);
    return Trim(out);
}

int CountPhysicalCores() {
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len == 0) return 0;
    std::vector<uint8_t> buf(len);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len))
        return 0;
    int cores = 0;
    uint8_t* p = buf.data();
    uint8_t* end = p + len;
    while (p < end) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(p);
        if (info->Relationship == RelationProcessorCore) ++cores;
        p += info->Size;
    }
    return cores;
}

GpuVendor ClassifyGpu(const std::wstring& name) {
    std::wstring n = ToLower(name);
    if (n.find(L"nvidia") != std::wstring::npos || n.find(L"geforce") != std::wstring::npos ||
        n.find(L"rtx") != std::wstring::npos || n.find(L"gtx") != std::wstring::npos)
        return GpuVendor::Nvidia;
    if (n.find(L"amd") != std::wstring::npos || n.find(L"radeon") != std::wstring::npos)
        return GpuVendor::Amd;
    if (n.find(L"intel") != std::wstring::npos || n.find(L"arc") != std::wstring::npos)
        return GpuVendor::Intel;
    return GpuVendor::Unknown;
}

std::wstring PrimaryGpuName() {
    // EnumDisplayDevices gives the adapter description of the primary display.
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            return dd.DeviceString;
        dd.cb = sizeof(dd);
    }
    // Fall back to first adapter.
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(nullptr, 0, &dd, 0)) return dd.DeviceString;
    return L"";
}

void GatherDisplays(HardwareInfo& hw) {
    int count = 0;
    int firstHz = 0;
    bool mismatch = false;
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i, dd = {sizeof(DISPLAY_DEVICEW)}) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
            ++count;
            int hz = (int)dm.dmDisplayFrequency;
            if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) hw.primaryRefreshHz = hz;
            if (firstHz == 0) firstHz = hz;
            else if (hz != firstHz) mismatch = true;
        }
    }
    hw.monitorCount = count;
    hw.multiMonitor = count > 1;
    hw.refreshMismatch = mismatch && count > 1;
    if (hw.primaryRefreshHz == 0) hw.primaryRefreshHz = firstHz;
}

} // namespace

HardwareInfo GatherHardwareInfo() {
    HardwareInfo hw;

    hw.cpuName = ReadRegString(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    hw.logicalCores = (int)si.dwNumberOfProcessors;
    hw.physicalCores = CountPhysicalCores();
    if (hw.physicalCores == 0) hw.physicalCores = hw.logicalCores;

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) hw.totalRamBytes = ms.ullTotalPhys;

    hw.gpuName = PrimaryGpuName();
    hw.gpuVendor = ClassifyGpu(hw.gpuName);

    std::wstring build = ReadRegString(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    std::wstring display = ReadRegString(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    hw.osVersion = build + (display.empty() ? L"" : (L" " + display));

    GatherDisplays(hw);
    return hw;
}

} // namespace fp
