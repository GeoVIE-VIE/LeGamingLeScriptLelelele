// SPDX-License-Identifier: MIT
#include "subprocess.h"
#include <shlwapi.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace fp {

namespace {

// Quote a single argument per the Windows command-line parsing rules used by
// the CRT / CommandLineToArgvW. This is the canonical algorithm and is what
// keeps us safe from argument-injection.
std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() &&
        arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg; // no quoting needed
    }
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') { ++it; ++backslashes; }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        } else if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(*it);
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}

std::wstring SelfDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : path.substr(0, slash);
}

} // namespace

std::wstring ResolveTool(const std::wstring& exeName) {
    const std::wstring dir = SelfDir();
    if (!dir.empty()) {
        std::wstring candidates[] = {
            dir + L"\\" + exeName,
            dir + L"\\tools\\" + exeName,
        };
        for (const auto& c : candidates) {
            if (PathFileExistsW(c.c_str())) return c;
        }
    }
    // Fall back to PATH lookup via SearchPath.
    wchar_t found[MAX_PATH];
    DWORD n = SearchPathW(nullptr, exeName.c_str(), nullptr, MAX_PATH, found, nullptr);
    if (n > 0 && n < MAX_PATH) return std::wstring(found, n);
    return L"";
}

ProcResult RunCaptured(const std::wstring& exePath,
                       const std::vector<std::wstring>& args,
                       DWORD timeoutMs,
                       size_t maxOutputBytes) {
    ProcResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return result;
    // The read end must NOT be inherited by the child.
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // Build a properly quoted command line. argv[0] is the program itself.
    std::wstring cmd = QuoteArg(exePath);
    for (const auto& a : args) { cmd += L' '; cmd += QuoteArg(a); }
    // CreateProcessW may modify the buffer, so use a mutable vector.
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError  = writePipe;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exePath.c_str(), cmdBuf.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    // Parent no longer needs the write end; closing it lets ReadFile see EOF.
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return result;
    }
    result.launched = true;

    // Drain stdout while the child runs.
    std::string& out = result.stdoutUtf8;
    char buf[4096];
    DWORD read = 0;
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            if (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0) {
                if (out.size() < maxOutputBytes)
                    out.append(buf, (std::min)((size_t)read, maxOutputBytes - out.size()));
                continue;
            }
        }
        DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) {
            // Flush any remaining buffered output.
            while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0 &&
                   ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0) {
                if (out.size() < maxOutputBytes)
                    out.append(buf, (std::min)((size_t)read, maxOutputBytes - out.size()));
            }
            break;
        }
        if (GetTickCount64() - start > timeoutMs) {
            result.timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    GetExitCodeProcess(pi.hProcess, &result.exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return result;
}

} // namespace fp
