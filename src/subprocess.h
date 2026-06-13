// SPDX-License-Identifier: MIT
//
// Minimal, injection-safe child-process runner.
//
// We deliberately never go through cmd.exe / system(). The executable path
// and each argument are passed explicitly and quoted by us, so a target
// process name containing spaces or shell metacharacters can never be
// interpreted as a command. stdout is captured through an anonymous pipe.
//
#pragma once
#include "common.h"
#include <string>
#include <vector>

namespace fp {

struct ProcResult {
    bool        launched = false;   // did CreateProcess succeed?
    bool        timedOut = false;
    DWORD       exitCode = (DWORD)-1;
    std::string stdoutUtf8;         // captured child stdout
};

// Resolve a tool either next to our own executable, in a bundled tools\\
// directory, or on PATH. Returns empty string if not found.
std::wstring ResolveTool(const std::wstring& exeName);

// Run exePath with the given argument vector. Arguments are quoted safely.
// timeoutMs guards against a hung child. Output capped at maxOutputBytes.
ProcResult RunCaptured(const std::wstring& exePath,
                       const std::vector<std::wstring>& args,
                       DWORD timeoutMs = 15000,
                       size_t maxOutputBytes = 8 * 1024 * 1024);

} // namespace fp
