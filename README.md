# FrameProbe

**Windows Frame-Time Interference Diagnostic** — a single-executable C++ tool
that figures out *what external condition* makes a latency-sensitive foreground
app or game stutter, then maps the likely troubleshooting area and the safest
settings to test.

It does **not** assume the game is the problem. The target is any foreground
process — `cs2.exe`, `valorant.exe`, `r5apex.exe`,
`FortniteClient-Win64-Shipping.exe`, `EscapeFromTarkov.exe`, `blender.exe`,
`obs64.exe`, emulators, CAD/3D, VR, streaming apps … whatever you point it at.

> Built by **geovie**. The `geovie` tag is watermarked subtly into the
> dashboard.

---

## What it measures

Per test run it collects, for the foreground app:

- Frame-time average, **p95**, **p99**, max (via PresentMon — DirectX, OpenGL,
  Vulkan, desktop, UWP)
- Spikes over **16.7 / 33.3 / 50 ms**
- CPU total, per-core, **target-process CPU**, **browser-process CPU**
- GPU utilization, **power draw**, clock, VRAM, video-decode usage
- Disk active time, memory pressure, hard page faults

The report never says "FPS is good." It states deltas and a conclusion, e.g.:

```
p99 worsened by 41% with browser open (closed 8.2 ms vs open 41.8 ms).
Browser CPU averaged 22% during the open run.
GPU stayed below 70% utilization.
=> Primary likely issue: browser/extension CPU scheduling interference.
=> Not likely: GPU saturation.
```

## The diagnostic decision tree

FrameProbe classifies the worst run + matrix comparison into one of:

1. **Browser interference** (sub-classified: HWA on vs off, video, extensions,
   multi-monitor)
2. **GPU saturation / throttle**
3. **CPU / main-thread stall**
4. **Shader / cache stall**
5. **Capture / overlay hook**
6. **Network jitter** (mistaken for stutter)

## Universal test matrix

Run the same scenario repeatedly and compare. Suggested ladder:

```
Run 1: Target only, no browser          Run 8:  Browser HWA OFF
Run 2: Browser open, blank tab          Run 9:  Overlays disabled
Run 3: Browser open, normal tabs        Run 10: Lower FPS cap
Run 4: Browser open, video playing      Run 11: Vendor profile baseline
Run 5: Browser open, minimized          Run 12: Shader cache cleared + warmed
Run 6: Browser extensions disabled      Run 13: HAGS opposite state
Run 7: Browser HWA ON
```

Pick the matching **Browser condition** / **Overlay condition** before each
"Start run"; results accumulate in the *Matrix Runs* panel and feed the
classifier.

## Safety model

Settings are split into three tiers, exactly as a diagnostic tool should:

- **Read-only / safe automatic** — hardware inventory, process & overlay
  detection, perf counters, PresentMon capture, report generation.
- **Guarded (confirmation required)** — clear shader caches, toggle browser
  hardware-acceleration *policy* (documented enterprise keys), HAGS, Game Bar
  capture. Every write is confirmed in a dialog first.
- **Report-only** — BIOS ReBAR/XMP/EXPO, GPU OC/undervolt, NVIDIA Control
  Panel profiles, G-SYNC/VRR mode. FrameProbe **reports and recommends** these
  rather than writing undocumented driver blobs.

The app runs **as the invoking user** (no forced elevation). HKLM policy
writes that need admin fail gracefully and tell you to relaunch elevated —
least privilege by default.

### Security notes

- No `system()` / `cmd.exe`. External tools (`nvidia-smi`, `PresentMon`) are
  launched with `CreateProcessW`, an explicit executable path, and individually
  quoted arguments — a target name can never be interpreted as a command.
- Shader-cache deletion only accepts paths the tool itself enumerated under
  `%LOCALAPPDATA%`; arbitrary paths are rejected.
- Subprocess output is size-capped; all format strings are literals (user text
  is only ever passed as data).

## Building

Requires Windows + Visual Studio 2019/2022 (or Build Tools) and CMake ≥ 3.16.

```bat
cmake -B build -A x64
cmake --build build --config Release
```

The result is `build\Release\FrameProbe.exe` — a self-contained GUI executable
(Win32 + GDI+ + PDH, no third-party runtime dependencies).

## Enabling frame-time capture

Frame-time stats need **PresentMon** (Microsoft's open-source capture tool).
Download `PresentMon.exe` and drop it next to `FrameProbe.exe` or in a
`tools\` folder beside it. Without it, FrameProbe still collects every
hardware/process metric and classifies on those — it just marks frame stats as
unavailable instead of fabricating them.

For deep root-cause work beyond this tool, capture ETW/WPR traces and analyze
with **GPUView / WPA**.

## Output

"Generate report" writes a timestamped `FrameProbe_Report_*.txt` to your
Documents folder containing the full target/system inventory, environment
state, every matrix run, the classification, and the ranked settings to test.
