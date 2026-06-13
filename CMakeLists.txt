# FrameProbe - Windows Frame-Time Interference Diagnostic
# Build (Windows, Visual Studio 2019/2022 toolset):
#   cmake -B build -A x64
#   cmake --build build --config Release
#
cmake_minimum_required(VERSION 3.16)
project(FrameProbe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "FrameProbe targets Windows (Win32 + GDI+ + PDH).")
endif()

set(SOURCES
    src/common.cpp
    src/subprocess.cpp
    src/process_monitor.cpp
    src/system_info.cpp
    src/perf_counters.cpp
    src/gpu_info.cpp
    src/frametime.cpp
    src/tweaks.cpp
    src/diagnostics.cpp
    src/report.cpp
    src/engine.cpp
    src/gui_theme.cpp
    src/main.cpp
)

# WIN32 => GUI subsystem (uses wWinMain, no console window).
add_executable(FrameProbe WIN32 ${SOURCES} res/app.manifest)

target_compile_definitions(FrameProbe PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
)

if(MSVC)
    # /W4 warnings, treat the binary as DPI/Unicode aware, secure CRT.
    target_compile_options(FrameProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(FrameProbe PRIVATE /MANIFEST:NO) # manifest supplied as a source
endif()

# Most Win32 libs are auto-linked via #pragma comment(lib, ...) in the sources,
# but list the GDI+ / PDH essentials here too for non-MSVC generators.
target_link_libraries(FrameProbe PRIVATE
    gdiplus pdh psapi shlwapi comctl32 shell32 ole32 user32 gdi32 advapi32
)
