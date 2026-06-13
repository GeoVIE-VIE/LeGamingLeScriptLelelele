// SPDX-License-Identifier: MIT
//
// Shared GDI+ drawing helpers and the dark dashboard palette. Keeping the
// theme in one header lets the watermark, cards and charts stay visually
// consistent.
//
#pragma once
#include "common.h"
#include <gdiplus.h>
#include <deque>

namespace fp {

namespace theme {
    using Gdiplus::Color;
    inline Color Bg()        { return Color(255,  16,  18,  26); }  // near-black navy
    inline Color BgPanel()   { return Color(255,  24,  27,  38); }
    inline Color Card()      { return Color(255,  31,  35,  49); }
    inline Color CardHi()    { return Color(255,  40,  45,  64); }
    inline Color Stroke()    { return Color(255,  52,  58,  82); }
    inline Color Text()      { return Color(255, 232, 236, 246); }
    inline Color TextDim()   { return Color(255, 150, 158, 180); }
    inline Color Accent()    { return Color(255,  94, 158, 255); }  // electric blue
    inline Color AccentSoft(){ return Color(255,  64, 196, 180); }  // teal
    inline Color Good()      { return Color(255,  80, 210, 130); }
    inline Color Warn()      { return Color(255, 240, 190,  90); }
    inline Color Bad()       { return Color(255, 240, 100, 110); }
    inline Color Watermark() { return Color( 22, 150, 170, 210); }  // very low alpha
}

// Anti-aliased rounded rectangle fill.
void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                   const Gdiplus::Color& fill);
void StrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                     const Gdiplus::Color& color, float width = 1.0f);

// Text helpers (DPI-agnostic; sizes are logical points scaled by caller).
// Named DrawStr to avoid the <windows.h> DrawText macro.
void DrawStr(Gdiplus::Graphics& g, const std::wstring& s, float x, float y,
             float sizePt, const Gdiplus::Color& c, bool bold = false,
             const wchar_t* family = L"Segoe UI");
void DrawStrRight(Gdiplus::Graphics& g, const std::wstring& s, float right, float y,
                  float sizePt, const Gdiplus::Color& c, bool bold = false);

// A small history sparkline.
void DrawSparkline(Gdiplus::Graphics& g, const Gdiplus::RectF& r,
                   const std::deque<double>& data, double maxVal,
                   const Gdiplus::Color& line, const Gdiplus::Color& fill);

// Horizontal value bar (0..max) with color.
void DrawBar(Gdiplus::Graphics& g, const Gdiplus::RectF& r, double value, double maxVal,
             const Gdiplus::Color& color);

// Pick a color for a metric based on thresholds (good/warn/bad).
Gdiplus::Color StatusColor(double value, double warnAt, double badAt, bool higherIsBad = true);

} // namespace fp
