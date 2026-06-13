// SPDX-License-Identifier: MIT
//
// FrameProbe - entry point, Win32 window, native input controls, and the
// GDI+ dashboard renderer (double-buffered). The "geovie" watermark is drawn
// subtly into the dashboard background and corner on every frame.
//
#include "common.h"
#include "engine.h"
#include "gui_theme.h"
#include "tweaks.h"
#include "report.h"
#include "diagnostics.h"
#include "process_monitor.h"
#include "frametime.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>  // ShellExecuteW (WIN32_LEAN_AND_MEAN omits it)
#include <objidl.h>   // IStream etc. - GDI+ needs it (WIN32_LEAN_AND_MEAN omits it)
#include <gdiplus.h>
#include <algorithm>
#include <deque>
#include <memory>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace fp;
using namespace Gdiplus;

// ----- control identifiers -------------------------------------------------
enum {
    IDC_TARGET = 1001, IDC_DETECT, IDC_DURATION, IDC_BROWSER, IDC_OVERLAY,
    IDC_GPU, IDC_DISPLAY, IDC_REFRESH, IDC_START, IDC_CANCEL, IDC_CLEAR,
    IDC_REPORT, IDC_EDGE_ON, IDC_EDGE_OFF, IDC_CHROME_ON, IDC_CHROME_OFF,
    IDC_HAGS_ON, IDC_HAGS_OFF, IDC_GAMEBAR_ON, IDC_GAMEBAR_OFF, IDC_SHADER,
};

namespace {

constexpr int kPanelW = 340;
constexpr UINT_PTR kTimerId = 1;

Engine*  g_engine = nullptr;
HFONT    g_uiFont = nullptr;
HFONT    g_lblFont = nullptr;
ULONG_PTR g_gdipToken = 0;

std::deque<double> g_cpuHist;
std::deque<double> g_gpuHist;
constexpr size_t kHistMax = 120;

std::vector<HWND> g_labels;   // panel section captions, positioned in order

// Map of created child control handles by id, for layout/reads.
struct Controls {
    HWND target=0, detect=0, duration=0, browser=0, overlay=0, gpu=0, display=0,
         refresh=0, start=0, cancel=0, clear=0, report=0,
         edgeOn=0, edgeOff=0, chromeOn=0, chromeOff=0, hagsOn=0, hagsOff=0,
         gamebarOn=0, gamebarOff=0, shader=0;
} g_c;

// ----- helpers -------------------------------------------------------------
HWND MakeLabel(HWND parent, const wchar_t* text, int id = -1) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_lblFont, TRUE);
    return h;
}
HWND MakeButton(HWND parent, const wchar_t* text, int id) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return h;
}
HWND MakeCombo(HWND parent, int id, bool editable = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                  (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST);
    HWND h = CreateWindowExW(0, L"COMBOBOX", L"", style,
                             0, 0, 10, 200, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return h;
}
HWND MakeEdit(HWND parent, int id, const wchar_t* text) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                             WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return h;
}
void ComboAdd(HWND combo, const wchar_t* s) { SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)s); }
void ComboSel(HWND combo, int i) { SendMessageW(combo, CB_SETCURSEL, i, 0); }
int  ComboGet(HWND combo) { return (int)SendMessageW(combo, CB_GETCURSEL, 0, 0); }

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize((size_t)n);
    return s;
}

void PopulateProcessCombo(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    auto procs = EnumerateProcesses();
    std::set<std::wstring> seen;
    for (const auto& p : procs) {
        if (p.image.empty() || seen.count(p.image)) continue;
        seen.insert(p.image);
    }
    for (const auto& img : seen) ComboAdd(combo, img.c_str());
}

// Build a RunConfig from the current control state.
RunConfig ReadConfig() {
    RunConfig cfg;
    cfg.targetProcess = Trim(GetText(g_c.target));
    cfg.durationSeconds = 30;
    {
        std::wstring d = GetText(g_c.duration);
        int v = _wtoi(d.c_str());
        if (v >= 5 && v <= 600) cfg.durationSeconds = v;
    }
    cfg.browser = (BrowserCondition)std::max(0, ComboGet(g_c.browser));
    cfg.overlay = (OverlayCondition)std::max(0, ComboGet(g_c.overlay));
    cfg.gpu     = (GpuVendor)std::max(0, ComboGet(g_c.gpu));
    cfg.display = (DisplayMode)std::max(0, ComboGet(g_c.display));
    cfg.refreshHz = _wtoi(GetText(g_c.refresh).c_str());
    cfg.label = std::wstring(ToString(cfg.browser)) + L" | " + ToString(cfg.overlay);
    return cfg;
}

// Confirm + apply a guarded registry tweak.
void ApplyTweak(HWND owner, bool ok, const wchar_t* what, bool rebootNote) {
    std::wstring msg;
    if (ok) {
        msg = std::wstring(what) + L" applied.";
        if (rebootNote) msg += L"\nA reboot/restart is required for this to take effect.";
    } else {
        msg = std::wstring(L"Failed to apply ") + what +
              L".\nThis usually means the app is not running as Administrator, "
              L"or the policy is managed elsewhere.";
    }
    MessageBoxW(owner, msg.c_str(), kAppName, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void DoShaderClear(HWND owner) {
    auto caches = EnumerateShaderCaches();
    std::wstring list;
    int existing = 0;
    for (auto& c : caches) if (c.exists) {
        list += L"  - " + c.label + L" (" +
                std::to_wstring(c.sizeBytes / (1024 * 1024)) + L" MB)\n";
        ++existing;
    }
    if (existing == 0) {
        MessageBoxW(owner, L"No shader caches found to clear.", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring msg = L"Clear the contents of these shader caches?\n\n" + list +
        L"\nClose the target app, launchers and browsers first.\n"
        L"IMPORTANT: the FIRST run after clearing will stutter while shaders "
        L"rebuild - do not score it. Reboot, warm up, then test.";
    if (MessageBoxW(owner, msg.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    long removed = 0;
    for (auto& c : caches) if (c.exists) {
        long r = ClearShaderCache(c.path);
        if (r > 0) removed += r;
    }
    std::wstring done = L"Removed " + std::to_wstring(removed) +
        L" cached files. Reboot, do a warm-up run, then score the 2nd/3rd run.";
    MessageBoxW(owner, done.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
}

void GenerateReport(HWND owner) {
    ReportContext ctx;
    ctx.hw = g_engine->Hardware();
    ctx.gpu = g_engine->LiveGpu();
    ctx.browserHwa = ReadBrowserHwa();
    ctx.compositor = ReadCompositorState();
    for (auto& b : DetectRunningBrowsers()) ctx.runningBrowsers.push_back(b);
    for (auto& o : DetectRunningOverlays()) ctx.runningOverlays.push_back(o);
    ctx.shaderCaches = EnumerateShaderCaches();
    ctx.runs = g_engine->Runs();
    ctx.presentMonAvailable = PresentMonAvailable();
    ctx.diagnosis = Diagnose(ctx.runs, ctx.hw);

    std::wstring text = BuildReport(ctx);
    std::wstring path = DefaultReportPath();
    if (WriteTextFileUtf8(path, text)) {
        std::wstring msg = L"Report written to:\n" + path + L"\n\nOpen it now?";
        if (MessageBoxW(owner, msg.c_str(), kAppName, MB_YESNO | MB_ICONINFORMATION) == IDYES)
            ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        MessageBoxW(owner, L"Failed to write report file.", kAppName, MB_OK | MB_ICONWARNING);
    }
}

// ----- control creation / layout ------------------------------------------
void CreateControls(HWND hwnd) {
    g_c.target  = MakeCombo(hwnd, IDC_TARGET, true);
    g_c.detect  = MakeButton(hwnd, L"Detect foreground", IDC_DETECT);
    g_c.duration= MakeCombo(hwnd, IDC_DURATION, true);
    g_c.browser = MakeCombo(hwnd, IDC_BROWSER);
    g_c.overlay = MakeCombo(hwnd, IDC_OVERLAY);
    g_c.gpu     = MakeCombo(hwnd, IDC_GPU);
    g_c.display = MakeCombo(hwnd, IDC_DISPLAY);
    g_c.refresh = MakeEdit(hwnd, IDC_REFRESH, L"");
    g_c.start   = MakeButton(hwnd, L"Start run", IDC_START);
    g_c.cancel  = MakeButton(hwnd, L"Cancel", IDC_CANCEL);
    g_c.clear   = MakeButton(hwnd, L"Clear runs", IDC_CLEAR);
    g_c.report  = MakeButton(hwnd, L"Generate report", IDC_REPORT);
    g_c.edgeOn   = MakeButton(hwnd, L"Edge HWA On", IDC_EDGE_ON);
    g_c.edgeOff  = MakeButton(hwnd, L"Edge HWA Off", IDC_EDGE_OFF);
    g_c.chromeOn = MakeButton(hwnd, L"Chrome HWA On", IDC_CHROME_ON);
    g_c.chromeOff= MakeButton(hwnd, L"Chrome HWA Off", IDC_CHROME_OFF);
    g_c.hagsOn   = MakeButton(hwnd, L"HAGS On", IDC_HAGS_ON);
    g_c.hagsOff  = MakeButton(hwnd, L"HAGS Off", IDC_HAGS_OFF);
    g_c.gamebarOn = MakeButton(hwnd, L"GameBar On", IDC_GAMEBAR_ON);
    g_c.gamebarOff= MakeButton(hwnd, L"GameBar Off", IDC_GAMEBAR_OFF);
    g_c.shader    = MakeButton(hwnd, L"Clear shader cache...", IDC_SHADER);

    // Duration presets.
    for (const wchar_t* d : {L"15", L"30", L"45", L"60", L"120"}) ComboAdd(g_c.duration, d);
    SetWindowTextW(g_c.duration, L"30");

    ComboAdd(g_c.browser, L"Closed");
    ComboAdd(g_c.browser, L"Open, blank tab");
    ComboAdd(g_c.browser, L"Open, normal tabs");
    ComboAdd(g_c.browser, L"Open, video playing");
    ComboAdd(g_c.browser, L"Open, minimized");
    ComboAdd(g_c.browser, L"Extensions disabled");
    ComboAdd(g_c.browser, L"HW accel ON");
    ComboAdd(g_c.browser, L"HW accel OFF");
    ComboSel(g_c.browser, 0);

    ComboAdd(g_c.overlay, L"Overlays normal");
    ComboAdd(g_c.overlay, L"Overlays disabled");
    ComboAdd(g_c.overlay, L"Background recording off");
    ComboSel(g_c.overlay, 0);

    ComboAdd(g_c.gpu, L"Unknown / auto");
    ComboAdd(g_c.gpu, L"NVIDIA");
    ComboAdd(g_c.gpu, L"AMD");
    ComboAdd(g_c.gpu, L"Intel");
    ComboSel(g_c.gpu, (int)g_engine->Hardware().gpuVendor);

    ComboAdd(g_c.display, L"Unknown");
    ComboAdd(g_c.display, L"Fullscreen exclusive");
    ComboAdd(g_c.display, L"Borderless");
    ComboAdd(g_c.display, L"Windowed");
    ComboSel(g_c.display, 0);

    PopulateProcessCombo(g_c.target);
    std::wstring fg = ForegroundProcessImage();
    if (!fg.empty() && ToLower(fg) != ToLower(std::wstring(kAppName) + L".exe"))
        SetWindowTextW(g_c.target, fg.c_str());

    wchar_t hz[16];
    swprintf(hz, 16, L"%d", g_engine->Hardware().primaryRefreshHz);
    SetWindowTextW(g_c.refresh, hz);

    // Section captions, created in the same order LayoutControls positions them.
    const wchar_t* caps[] = {
        L"Target",
        L"Duration (s)  /  Refresh (Hz)",
        L"Browser condition",
        L"Overlay condition",
        L"GPU vendor  /  Display mode",
        L"Tweaks  (admin needed for some)",
    };
    for (const wchar_t* c : caps) g_labels.push_back(MakeLabel(hwnd, c));
}

void Move(HWND h, int x, int y, int w, int hgt) { MoveWindow(h, x, y, w, hgt, TRUE); }

void LayoutControls(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    const int pad = 16;
    const int x = pad;
    const int w = kPanelW - pad * 2;
    int y = 96;              // below header
    const int rowH = 26, gap = 8, lblH = 18;

    size_t labelIdx = 0;
    auto label = [&](const wchar_t*) {
        if (labelIdx < g_labels.size())
            Move(g_labels[labelIdx++], x, y, w, lblH);
        y += lblH;
    };
    auto row = [&](HWND h) { Move(h, x, y, w, rowH); y += rowH + gap; };
    auto halfRow = [&](HWND a, HWND b) {
        int hw = (w - gap) / 2;
        Move(a, x, y, hw, rowH);
        Move(b, x + hw + gap, y, hw, rowH);
        y += rowH + gap;
    };

    label(L"Target"); row(g_c.target);
    Move(g_c.detect, x, y, w, rowH); y += rowH + gap + 4;

    int hw = (w - gap) / 2;
    label(L"Duration (s) / Refresh (Hz)");
    Move(g_c.duration, x, y, hw, rowH);
    Move(g_c.refresh, x + hw + gap, y, hw, rowH); y += rowH + gap;

    label(L"Browser condition"); row(g_c.browser);
    label(L"Overlay condition"); row(g_c.overlay);
    label(L"GPU vendor / Display");
    Move(g_c.gpu, x, y, hw, rowH);
    Move(g_c.display, x + hw + gap, y, hw, rowH); y += rowH + gap + 4;

    halfRow(g_c.start, g_c.cancel);
    halfRow(g_c.clear, g_c.report);

    y += 6;
    label(L"Tweaks (admin needed for some)");
    halfRow(g_c.edgeOn, g_c.edgeOff);
    halfRow(g_c.chromeOn, g_c.chromeOff);
    halfRow(g_c.hagsOn, g_c.hagsOff);
    halfRow(g_c.gamebarOn, g_c.gamebarOff);
    Move(g_c.shader, x, y, w, rowH);
}

// ----- dashboard rendering -------------------------------------------------
void DrawWatermark(Graphics& g, const RectF& area) {
    // Large faint diagonal brand in the dashboard background.
    FontFamily ff(L"Segoe UI");
    Font big(&ff, area.Height * 0.34f, FontStyleBold, UnitPixel);
    SolidBrush faint(theme::Watermark());
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    GraphicsState st = g.Save();
    g.TranslateTransform(area.X + area.Width / 2, area.Y + area.Height / 2);
    g.RotateTransform(-18.0f);
    RectF box(-area.Width / 2, -area.Height / 2, area.Width, area.Height);
    g.DrawString(kWatermark, -1, &big, box, &fmt, &faint);
    g.Restore(st);
}

void DrawCard(Graphics& g, const RectF& r, const std::wstring& title) {
    FillRoundRect(g, r, 10.0f, theme::Card());
    StrokeRoundRect(g, r, 10.0f, theme::Stroke(), 1.0f);
    DrawStr(g, title, r.X + 14, r.Y + 10, 12.0f, theme::TextDim(), true);
}

void DrawMetricCard(Graphics& g, const RectF& r, const std::wstring& title,
                    const std::wstring& value, const std::wstring& sub,
                    double barVal, double barMax, const Color& barColor) {
    DrawCard(g, r, title);
    DrawStr(g, value, r.X + 14, r.Y + 30, 26.0f, theme::Text(), true);
    if (barMax > 0)
        DrawBar(g, RectF(r.X + 14, r.Y + r.Height - 26, r.Width - 28, 8), barVal, barMax, barColor);
    if (!sub.empty())
        DrawStr(g, sub, r.X + 14, r.Y + r.Height - 48, 10.5f, theme::TextDim());
}

std::wstring Fmt0(double v) {
    if (v < 0) return L"n/a";
    wchar_t b[32]; swprintf(b, 32, L"%.0f", v); return b;
}
std::wstring Fmt1(double v) {
    if (v < 0) return L"n/a";
    wchar_t b[32]; swprintf(b, 32, L"%.1f", v); return b;
}

void RenderDashboard(HDC hdc, RECT client) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    const float W = (float)(client.right - client.left);
    const float H = (float)(client.bottom - client.top);

    // Background.
    SolidBrush bg(theme::Bg());
    g.FillRectangle(&bg, 0.0f, 0.0f, W, H);

    // Left config panel backdrop.
    SolidBrush panel(theme::BgPanel());
    g.FillRectangle(&panel, 0.0f, 0.0f, (float)kPanelW, H);
    Pen sep(theme::Stroke(), 1.0f);
    g.DrawLine(&sep, (float)kPanelW, 0.0f, (float)kPanelW, H);

    // Header.
    LinearGradientBrush hb(RectF(0, 0, W, 84),
        Color(255, 28, 32, 48), Color(255, 18, 20, 30), LinearGradientModeHorizontal);
    g.FillRectangle(&hb, 0.0f, 0.0f, W, 84.0f);
    DrawStr(g, kAppName, 16, 16, 24.0f, theme::Text(), true);
    DrawStr(g, kAppTagline, 18, 50, 11.5f, theme::TextDim());

    HardwareInfo hw = g_engine->Hardware();
    std::wstring hwline = hw.cpuName + L"   |   " + hw.gpuName + L"   |   " +
        std::to_wstring(hw.totalRamBytes / (1024ull*1024*1024)) + L" GB RAM   |   " +
        std::to_wstring(hw.primaryRefreshHz) + L" Hz";
    DrawStrRight(g, hwline, W - 18, 22, 10.5f, theme::TextDim());
    std::wstring status = g_engine->StatusText();
    if (g_engine->IsRunning()) {
        double p = g_engine->RunProgress();
        wchar_t b[64]; swprintf(b, 64, L"  (%.0f%%)", p * 100.0);
        status += b;
    }
    DrawStrRight(g, status, W - 18, 50, 11.0f,
                  g_engine->IsRunning() ? theme::Accent() : theme::TextDim());

    // Dashboard area starts to the right of the panel.
    const float dx = (float)kPanelW + 16;
    const float dy = 100;
    const float dw = W - dx - 16;

    DrawWatermark(g, RectF(dx, dy, dw, H - dy - 16));

    Snapshot s = g_engine->LiveSnapshot();
    GpuTelemetry gpu = g_engine->LiveGpu();

    // ---- top metric cards row ----
    const float cardH = 110, cgap = 14;
    const int cols = 4;
    const float cw = (dw - cgap * (cols - 1)) / cols;

    double maxCore = 0; for (double c : s.cpuPerCorePct) maxCore = (std::max)(maxCore, c);

    DrawMetricCard(g, RectF(dx + 0*(cw+cgap), dy, cw, cardH), L"CPU TOTAL",
        Fmt0(s.cpuTotalPct) + L"%", L"max core " + Fmt0(maxCore) + L"%",
        s.cpuTotalPct, 100, StatusColor(s.cpuTotalPct, 70, 90));
    DrawMetricCard(g, RectF(dx + 1*(cw+cgap), dy, cw, cardH), L"GPU UTIL",
        Fmt0(s.gpuUtilPct) + L"%",
        gpu.valid ? (Fmt0(gpu.powerW) + L" W / " + Fmt0(gpu.powerCapW) + L" W") : L"power n/a",
        s.gpuUtilPct < 0 ? 0 : s.gpuUtilPct, 100,
        StatusColor(s.gpuUtilPct, 95, 99));
    DrawMetricCard(g, RectF(dx + 2*(cw+cgap), dy, cw, cardH), L"BROWSER CPU",
        Fmt0(s.browserCpuPct) + L"%", L"target " + Fmt0(s.targetCpuPct) + L"%",
        s.browserCpuPct, 100, StatusColor(s.browserCpuPct, 10, 25));
    DrawMetricCard(g, RectF(dx + 3*(cw+cgap), dy, cw, cardH), L"MEMORY",
        Fmt0(s.memUsedPct) + L"%",
        L"hard faults/s " + std::to_wstring(s.hardFaultsPerSec),
        s.memUsedPct, 100, StatusColor(s.memUsedPct, 80, 92));

    // ---- second row: GPU detail + history sparklines ----
    float y2 = dy + cardH + cgap;
    float rowH2 = 150;
    float halfW = (dw - cgap) / 2;

    // GPU detail card.
    RectF gpuCard(dx, y2, halfW, rowH2);
    DrawCard(g, gpuCard, L"GPU DETAIL");
    {
        float ty = gpuCard.Y + 38, tx = gpuCard.X + 14;
        auto line2 = [&](const std::wstring& k, const std::wstring& v) {
            DrawStr(g, k, tx, ty, 11.5f, theme::TextDim());
            DrawStrRight(g, v, gpuCard.GetRight() - 14, ty, 11.5f, theme::Text());
            ty += 22;
        };
        line2(L"Clock", gpu.valid ? Fmt0(gpu.clockMhz) + L" MHz" : L"n/a");
        line2(L"VRAM", gpu.valid ? (Fmt0(gpu.memUsedMb) + L" / " + Fmt0(gpu.memTotalMb) + L" MB") : L"n/a");
        line2(L"Temp", gpu.valid ? Fmt0(gpu.tempC) + L" C" : L"n/a");
        line2(L"Video decode", Fmt0(s.gpuVideoDecodePct) + L"%");
        line2(L"Disk active", Fmt0(s.diskActivePct) + L"%");
    }

    // History card with two sparklines.
    RectF hist(dx + halfW + cgap, y2, halfW, rowH2);
    DrawCard(g, hist, L"LIVE HISTORY (CPU / GPU)");
    DrawSparkline(g, RectF(hist.X + 14, hist.Y + 36, hist.Width - 28, (rowH2 - 56) / 2 - 4),
                  g_cpuHist, 100.0, theme::Accent(), Color(40, 94, 158, 255));
    DrawStr(g, L"CPU", hist.X + 16, hist.Y + 34, 9.5f, theme::Accent());
    DrawSparkline(g, RectF(hist.X + 14, hist.Y + 36 + (rowH2 - 56) / 2 + 6, hist.Width - 28,
                           (rowH2 - 56) / 2 - 4),
                  g_gpuHist, 100.0, theme::AccentSoft(), Color(40, 64, 196, 180));
    DrawStr(g, L"GPU", hist.X + 16, hist.Y + 36 + (rowH2 - 56) / 2 + 4, 9.5f, theme::AccentSoft());

    // ---- third row: classification + runs ----
    float y3 = y2 + rowH2 + cgap;
    float bottomH = H - y3 - 16;
    if (bottomH < 120) bottomH = 120;

    // Classification (left).
    RectF clazz(dx, y3, halfW, bottomH);
    DrawCard(g, clazz, L"CLASSIFICATION");
    {
        auto runs = g_engine->Runs();
        Diagnosis d = Diagnose(runs, hw);
        float ty = clazz.Y + 36, tx = clazz.X + 14;
        Color pc = theme::Accent();
        DrawStr(g, std::wstring(L"Primary: ") + ToString(d.primary), tx, ty, 13.0f, pc, true);
        ty += 24;
        DrawStr(g, std::wstring(L"Secondary: ") + ToString(d.secondary), tx, ty, 11.5f, theme::TextDim());
        ty += 24;
        int shown = 0;
        for (const auto& e : d.evidence) {
            if (ty > clazz.GetBottom() - 22) break;
            DrawStr(g, L"- " + e, tx, ty, 10.5f, theme::Text());
            ty += 18; if (++shown > 8) break;
        }
        if (runs.empty()) {
            DrawStr(g, L"Run the matrix (closed vs open, overlay on/off, HWA on/off)",
                     tx, clazz.GetBottom() - 36, 10.0f, theme::TextDim());
            DrawStr(g, L"to populate evidence.", tx, clazz.GetBottom() - 20, 10.0f, theme::TextDim());
        }
    }

    // Runs list (right).
    RectF rl(dx + halfW + cgap, y3, halfW, bottomH);
    DrawCard(g, rl, L"MATRIX RUNS");
    {
        auto runs = g_engine->Runs();
        float ty = rl.Y + 34, tx = rl.X + 14;
        if (runs.empty()) {
            DrawStr(g, L"No runs yet.", tx, ty, 11.0f, theme::TextDim());
        }
        int idx = 1;
        for (const auto& r : runs) {
            if (ty > rl.GetBottom() - 30) break;
            std::wstring head = std::to_wstring(idx++) + L". " + r.config.label;
            DrawStr(g, head, tx, ty, 11.0f, theme::Text(), true); ty += 18;
            std::wstring stat = r.frames.valid
                ? (L"p99 " + Fmt1(r.frames.p99Ms) + L"ms  avg " + Fmt1(r.frames.avgMs) +
                   L"ms  >33ms:" + std::to_wstring(r.frames.spikes33))
                : L"frametime n/a (PresentMon?)";
            Color sc = r.frames.valid ? StatusColor(r.frames.p99Ms, 16.7, 33.3) : theme::TextDim();
            DrawStr(g, stat, tx + 8, ty, 10.0f, sc); ty += 16;
            std::wstring hw2 = L"CPU " + Fmt0(r.cpuTotalAvg) + L"%  GPU " + Fmt0(r.gpuUtilAvg) +
                L"%  brsr " + Fmt0(r.browserCpuAvg) + L"%";
            DrawStr(g, hw2, tx + 8, ty, 10.0f, theme::TextDim()); ty += 20;
        }
    }

    // Corner watermark (always, very subtle).
    DrawStrRight(g, std::wstring(kWatermark) + L"  -  " + kVersion, W - 14, H - 22, 10.0f,
                  Color(90, 120, 150, 190));
}

void OnTimer(HWND hwnd) {
    Snapshot s = g_engine->LiveSnapshot();
    g_cpuHist.push_back(s.cpuTotalPct);
    g_gpuHist.push_back(s.gpuUtilPct < 0 ? 0 : s.gpuUtilPct);
    while (g_cpuHist.size() > kHistMax) g_cpuHist.pop_front();
    while (g_gpuHist.size() > kHistMax) g_gpuHist.pop_front();
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hwnd);
        LayoutControls(hwnd);
        SetTimer(hwnd, kTimerId, 300, nullptr);
        return 0;

    case WM_SIZE:
        LayoutControls(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 1000;
        mmi->ptMinTrackSize.y = 700;
        return 0;
    }

    case WM_TIMER:
        if (wp == kTimerId) OnTimer(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(150, 158, 180));
        static HBRUSH br = CreateSolidBrush(RGB(24, 27, 38));
        return (LRESULT)br;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        switch (id) {
        case IDC_DETECT: {
            // Bring user's previous foreground app: re-scan running processes.
            PopulateProcessCombo(g_c.target);
            std::wstring fg = ForegroundProcessImage();
            if (!fg.empty()) SetWindowTextW(g_c.target, fg.c_str());
            break;
        }
        case IDC_START: {
            RunConfig cfg = ReadConfig();
            if (cfg.targetProcess.empty()) {
                MessageBoxW(hwnd, L"Enter or detect a target process first.", kAppName,
                            MB_OK | MB_ICONWARNING);
                break;
            }
            if (!PresentMonAvailable()) {
                MessageBoxW(hwnd,
                    L"PresentMon was not found, so frame-time stats will be unavailable "
                    L"for this run (hardware metrics will still be collected).\n\n"
                    L"Place PresentMon.exe in the app's tools\\ folder to enable frame capture.",
                    kAppName, MB_OK | MB_ICONINFORMATION);
            }
            g_engine->QueueRun(cfg);
            break;
        }
        case IDC_CANCEL:  g_engine->CancelRun(); break;
        case IDC_CLEAR:   g_engine->ClearRuns(); break;
        case IDC_REPORT:  GenerateReport(hwnd); break;
        case IDC_EDGE_ON:    ApplyTweak(hwnd, SetEdgeHwa(true),  L"Edge HW accel ON", false); break;
        case IDC_EDGE_OFF:   ApplyTweak(hwnd, SetEdgeHwa(false), L"Edge HW accel OFF", false); break;
        case IDC_CHROME_ON:  ApplyTweak(hwnd, SetChromeHwa(true), L"Chrome HW accel ON", false); break;
        case IDC_CHROME_OFF: ApplyTweak(hwnd, SetChromeHwa(false),L"Chrome HW accel OFF", false); break;
        case IDC_HAGS_ON:    ApplyTweak(hwnd, SetHags(true),  L"HAGS On", true); break;
        case IDC_HAGS_OFF:   ApplyTweak(hwnd, SetHags(false), L"HAGS Off", true); break;
        case IDC_GAMEBAR_ON: ApplyTweak(hwnd, SetGameBarEnabled(true),  L"Game Bar capture On", false); break;
        case IDC_GAMEBAR_OFF:ApplyTweak(hwnd, SetGameBarEnabled(false), L"Game Bar capture Off", false); break;
        case IDC_SHADER:     DoShaderClear(hwnd); break;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;  // we paint everything ourselves (double-buffered)

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        // Double buffer.
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        RenderDashboard(mem, rc);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GdiplusStartupInput gdipIn;
    if (GdiplusStartup(&g_gdipToken, &gdipIn, nullptr) != Ok) return 1;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lf = ncm.lfMessageFont; lf.lfWeight = FW_SEMIBOLD;
    g_lblFont = CreateFontIndirectW(&lf);

    Engine engine;
    engine.Start();
    g_engine = &engine;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"FrameProbeWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"FrameProbe - Frame-Time Interference Diagnostic  (geovie)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) { GdiplusShutdown(g_gdipToken); return 1; }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    engine.Stop();
    g_engine = nullptr;
    if (g_uiFont) DeleteObject(g_uiFont);
    if (g_lblFont) DeleteObject(g_lblFont);
    GdiplusShutdown(g_gdipToken);
    return (int)msg.wParam;
}
