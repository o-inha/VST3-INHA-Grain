#pragma once
#include <gdiplus.h>
#include <initializer_list>

namespace inha::grainIcons
{
enum class Icon { trigger, dices };
// Geometry from the supplied 24px trigger-guard.svg and Lucide dices.svg.
inline void draw (Gdiplus::Graphics& graphics, Icon icon, float left, float top,
                  float size, Gdiplus::Color ink)
{
    const auto state = graphics.Save();
    const auto scale = size / 24.0f;
    Gdiplus::Matrix transform (scale, 0.0f, 0.0f, scale, left, top);
    graphics.MultiplyTransform (&transform, Gdiplus::MatrixOrderPrepend);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen (ink, 2.0f);
    pen.SetStartCap (Gdiplus::LineCapRound);
    pen.SetEndCap (Gdiplus::LineCapRound);
    pen.SetLineJoin (Gdiplus::LineJoinRound);
    Gdiplus::GraphicsPath path;
    if (icon == Icon::trigger)
    {
        path.AddEllipse (2.0f, 5.0f, 20.0f, 14.0f);
        path.StartFigure();
        path.AddBezier (12.0f, 5.0f, 12.0f, 10.5f, 11.5f, 13.0f, 9.0f, 15.0f);
        path.AddBezier (9.0f, 15.0f, 14.0f, 15.0f, 17.0f, 11.0f, 17.0f, 5.938f);
    }
    else
    {
        path.AddArc (2.0f, 10.0f, 4.0f, 4.0f, 180.0f, 90.0f);
        path.AddArc (10.0f, 10.0f, 4.0f, 4.0f, 270.0f, 90.0f);
        path.AddArc (10.0f, 18.0f, 4.0f, 4.0f, 0.0f, 90.0f);
        path.AddArc (2.0f, 18.0f, 4.0f, 4.0f, 90.0f, 90.0f);
        path.CloseFigure();
        path.StartFigure();
        path.AddLine (17.92f, 14.0f, 21.42f, 10.5f);
        path.AddBezier (21.42f, 10.5f, 22.26f, 9.67f, 22.26f, 8.33f, 21.42f, 7.5f);
        path.AddLine (21.42f, 7.5f, 16.42f, 2.58f);
        path.AddBezier (16.42f, 2.58f, 15.59f, 1.74f, 14.25f, 1.74f, 13.42f, 2.58f);
        path.AddLine (13.42f, 2.58f, 10.0f, 6.0f);
        for (const auto point : { Gdiplus::PointF (6, 18), Gdiplus::PointF (10, 14),
                                  Gdiplus::PointF (15, 6), Gdiplus::PointF (18, 9) })
        {
            path.StartFigure();
            path.AddLine (point.X, point.Y, point.X + 0.01f, point.Y);
        }
    }
    graphics.DrawPath (&pen, &path);
    graphics.Restore (state);
}
}
