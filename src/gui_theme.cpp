// SPDX-License-Identifier: MIT
#include "gui_theme.h"
#include <algorithm>

using namespace Gdiplus;

namespace fp {

// GraphicsPath's copy ctor is protected, so it cannot be returned by value.
// Build into a caller-owned path instead.
static void MakeRoundPath(GraphicsPath& p, const RectF& r, float radius) {
    float d = radius * 2.0f;
    if (d > r.Width)  d = r.Width;
    if (d > r.Height) d = r.Height;
    if (d <= 0.5f) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRoundRect(Graphics& g, const RectF& r, float radius, const Color& fill) {
    GraphicsPath p;
    MakeRoundPath(p, r, radius);
    SolidBrush b(fill);
    g.FillPath(&b, &p);
}

void StrokeRoundRect(Graphics& g, const RectF& r, float radius, const Color& color, float width) {
    GraphicsPath p;
    MakeRoundPath(p, r, radius);
    Pen pen(color, width);
    g.DrawPath(&pen, &p);
}

void DrawStr(Graphics& g, const std::wstring& s, float x, float y, float sizePt,
             const Color& c, bool bold, const wchar_t* family) {
    FontFamily ff(family);
    Font font(&ff, sizePt, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush b(c);
    PointF pt(x, y);
    g.DrawString(s.c_str(), -1, &font, pt, &b);
}

void DrawStrRight(Graphics& g, const std::wstring& s, float right, float y, float sizePt,
                  const Color& c, bool bold) {
    FontFamily ff(L"Segoe UI");
    Font font(&ff, sizePt, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    RectF bound;
    g.MeasureString(s.c_str(), -1, &font, PointF(0, 0), &bound);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, &font, PointF(right - bound.Width, y), &b);
}

void DrawBar(Graphics& g, const RectF& r, double value, double maxVal, const Color& color) {
    FillRoundRect(g, r, r.Height / 2.0f, Color(255, 22, 25, 36));
    if (maxVal <= 0) return;
    double frac = std::clamp(value / maxVal, 0.0, 1.0);
    if (frac <= 0.001) return;
    RectF fillR(r.X, r.Y, (float)(r.Width * frac), r.Height);
    LinearGradientBrush grad(fillR,
        Color(color.GetA(), color.GetR(), color.GetG(), color.GetB()),
        Color(color.GetA(),
              (BYTE)std::min(255, color.GetR() + 40),
              (BYTE)std::min(255, color.GetG() + 40),
              (BYTE)std::min(255, color.GetB() + 40)),
        LinearGradientModeHorizontal);
    GraphicsPath p;
    MakeRoundPath(p, fillR, fillR.Height / 2.0f);
    g.FillPath(&grad, &p);
}

void DrawSparkline(Graphics& g, const RectF& r, const std::deque<double>& data,
                   double maxVal, const Color& line, const Color& fill) {
    if (data.size() < 2 || maxVal <= 0) return;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const size_t n = data.size();
    float dx = r.Width / (float)(n - 1);
    std::vector<PointF> pts;
    pts.reserve(n + 2);
    for (size_t i = 0; i < n; ++i) {
        double v = std::clamp(data[i] / maxVal, 0.0, 1.0);
        float x = r.X + dx * (float)i;
        float y = r.GetBottom() - (float)(v * r.Height);
        pts.push_back(PointF(x, y));
    }
    // Filled area under the line.
    std::vector<PointF> area = pts;
    area.push_back(PointF(r.GetRight(), r.GetBottom()));
    area.push_back(PointF(r.X, r.GetBottom()));
    SolidBrush fb(fill);
    g.FillPolygon(&fb, area.data(), (INT)area.size());
    Pen pen(line, 1.6f);
    pen.SetLineJoin(LineJoinRound);
    g.DrawLines(&pen, pts.data(), (INT)pts.size());
}

Color StatusColor(double value, double warnAt, double badAt, bool higherIsBad) {
    if (value < 0) return theme::TextDim();
    bool warn, bad;
    if (higherIsBad) { warn = value >= warnAt; bad = value >= badAt; }
    else             { warn = value <= warnAt; bad = value <= badAt; }
    if (bad)  return theme::Bad();
    if (warn) return theme::Warn();
    return theme::Good();
}

} // namespace fp
