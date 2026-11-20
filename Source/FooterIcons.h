#pragma once

#include <gdiplus.h>

namespace inha::footerIcons
{
enum class Icon
{
    settings,
    importFile,
    exportLocation,
    sun,
    moon,
    zoomIn,
    zoomOut
};

// Shared geometry from Ryoji PR #13's 24 x 24 Lucide icons. SVG arcs
// are converted to cubic Beziers of at most 90 degrees per segment.
inline void draw (Gdiplus::Graphics& graphics, Icon icon, float left, float top,
                  float size, Gdiplus::Color ink)
{
    if (size <= 0.0f)
        return;

    const auto savedState = graphics.Save ();
    const float scale = size / 24.0f;
    Gdiplus::Matrix transform (scale, 0.0f, 0.0f, scale, left, top);
    graphics.MultiplyTransform (&transform, Gdiplus::MatrixOrderPrepend);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen stroke (ink, 2.0f);
    stroke.SetStartCap (Gdiplus::LineCapRound);
    stroke.SetEndCap (Gdiplus::LineCapRound);
    stroke.SetLineJoin (Gdiplus::LineJoinRound);
    Gdiplus::GraphicsPath path;

    switch (icon)
    {
        case Icon::settings:
            path.StartFigure ();
            path.AddBezier (9.671f, 4.136f, 9.78517381f, 2.9348441f, 10.79393001f, 2.01742662f, 12.0005f, 2.01742662f);
            path.AddBezier (12.0005f, 2.01742662f, 13.20706999f, 2.01742662f, 14.21582619f, 2.9348441f, 14.33f, 4.136f);
            path.AddBezier (14.33f, 4.136f, 14.39720965f, 4.89594096f, 14.8306606f, 5.57540643f, 15.49146325f, 5.95667702f);
            path.AddBezier (15.49146325f, 5.95667702f, 16.15226589f, 6.3379476f, 16.95744535f, 6.37314836f, 17.649f, 6.051f);
            path.AddBezier (17.649f, 6.051f, 18.74524757f, 5.55329241f, 20.04024682f, 5.96863483f, 20.64251659f, 7.01110435f);
            path.AddBezier (20.64251659f, 7.01110435f, 21.24478636f, 8.05357387f, 20.9577482f, 9.38291261f, 19.979f, 10.084f);
            path.AddBezier (19.979f, 10.084f, 19.3547031f, 10.52204948f, 18.98304604f, 11.23685069f, 18.98304604f, 11.9995f);
            path.AddBezier (18.98304604f, 11.9995f, 18.98304604f, 12.76214931f, 19.3547031f, 13.47695052f, 19.979f, 13.915f);
            path.AddBezier (19.979f, 13.915f, 20.9577482f, 14.61608739f, 21.24478636f, 15.94542613f, 20.64251659f, 16.98789565f);
            path.AddBezier (20.64251659f, 16.98789565f, 20.04024682f, 18.03036517f, 18.74524757f, 18.44570759f, 17.649f, 17.948f);
            path.AddBezier (17.649f, 17.948f, 16.95744535f, 17.62585164f, 16.15226589f, 17.6610524f, 15.49146325f, 18.04232298f);
            path.AddBezier (15.49146325f, 18.04232298f, 14.8306606f, 18.42359357f, 14.39720965f, 19.10305904f, 14.33f, 19.863f);
            path.AddBezier (14.33f, 19.863f, 14.21582619f, 21.0641559f, 13.20706999f, 21.98157338f, 12.0005f, 21.98157338f);
            path.AddBezier (12.0005f, 21.98157338f, 10.79393001f, 21.98157338f, 9.78517381f, 21.0641559f, 9.671f, 19.863f);
            path.AddBezier (9.671f, 19.863f, 9.60391287f, 19.10278062f, 9.17031616f, 18.42304044f, 8.50923291f, 18.04172284f);
            path.AddBezier (8.50923291f, 18.04172284f, 7.84814965f, 17.66040524f, 7.04265027f, 17.62542584f, 6.351f, 17.948f);
            path.AddBezier (6.351f, 17.948f, 5.25475243f, 18.44570759f, 3.95975318f, 18.03036517f, 3.35748341f, 16.98789565f);
            path.AddBezier (3.35748341f, 16.98789565f, 2.75521364f, 15.94542613f, 3.0422518f, 14.61608739f, 4.021f, 13.915f);
            path.AddBezier (4.021f, 13.915f, 4.6452969f, 13.47695052f, 5.01695396f, 12.76214931f, 5.01695396f, 11.9995f);
            path.AddBezier (5.01695396f, 11.9995f, 5.01695396f, 11.23685069f, 4.6452969f, 10.52204948f, 4.021f, 10.084f);
            path.AddBezier (4.021f, 10.084f, 3.04367889f, 9.38259771f, 2.75739653f, 8.05444922f, 3.35898424f, 7.01271319f);
            path.AddBezier (3.35898424f, 7.01271319f, 3.96057194f, 5.97097716f, 5.25400799f, 5.55509352f, 6.35f, 6.051f);
            path.AddBezier (6.35f, 6.051f, 7.04155465f, 6.37314836f, 7.84673411f, 6.3379476f, 8.50753675f, 5.95667702f);
            path.AddBezier (8.50753675f, 5.95667702f, 9.1683394f, 5.57540643f, 9.60179035f, 4.89594096f, 9.669f, 4.136f);
            path.StartFigure ();
            path.AddEllipse (9.0f, 9.0f, 6.0f, 6.0f);
            break;
        case Icon::importFile:
            path.StartFigure ();
            path.AddLine (6.0f, 14.0f, 7.5f, 11.1f);
            path.AddBezier (7.5f, 11.1f, 7.83228802f, 10.44007851f, 8.50133987f, 10.0171147f, 9.24f, 10.0f);
            path.AddLine (9.24f, 10.0f, 20.0f, 10.0f);
            path.AddBezier (20.0f, 10.0f, 20.6191913f, 9.99891359f, 21.2039687f, 10.28468475f, 21.58357387f, 10.77386667f);
            path.AddBezier (21.58357387f, 10.77386667f, 21.96317905f, 11.2630486f, 22.09479902f, 11.90046993f, 21.94f, 12.5f);
            path.AddLine (21.94f, 12.5f, 20.4f, 18.5f);
            path.AddBezier (20.4f, 18.5f, 20.17076036f, 19.38792759f, 19.36702122f, 20.00618846f, 18.45f, 20.0f);
            path.AddLine (18.45f, 20.0f, 4.0f, 20.0f);
            path.AddBezier (4.0f, 20.0f, 2.8954305f, 20.0f, 2.0f, 19.1045695f, 2.0f, 18.0f);
            path.AddLine (2.0f, 18.0f, 2.0f, 5.0f);
            path.AddBezier (2.0f, 5.0f, 2.0f, 3.8954305f, 2.8954305f, 3.0f, 4.0f, 3.0f);
            path.AddLine (4.0f, 3.0f, 7.9f, 3.0f);
            path.AddBezier (7.9f, 3.0f, 8.57966741f, 2.99333642f, 9.21619763f, 3.33231702f, 9.59f, 3.9f);
            path.AddLine (9.59f, 3.9f, 10.4f, 5.1f);
            path.AddBezier (10.4f, 5.1f, 10.76992247f, 5.66172033f, 11.39741368f, 5.99988925f, 12.07f, 6.0f);
            path.AddLine (12.07f, 6.0f, 18.0f, 6.0f);
            path.AddBezier (18.0f, 6.0f, 19.1045695f, 6.0f, 20.0f, 6.8954305f, 20.0f, 8.0f);
            path.AddLine (20.0f, 8.0f, 20.0f, 10.0f);
            break;
        case Icon::exportLocation:
            path.StartFigure ();
            path.AddLine (13.33f, 13.0f, 8.0f, 13.0f);
            path.AddBezier (8.0f, 13.0f, 7.44771525f, 13.0f, 7.0f, 13.44771525f, 7.0f, 14.0f);
            path.AddLine (7.0f, 14.0f, 7.0f, 21.0f);
            path.StartFigure ();
            path.AddBezier (14.363f, 17.634f, 14.12524733f, 17.87162175f, 13.95122068f, 18.16533471f, 13.857f, 18.488f);
            path.AddLine (13.857f, 18.488f, 13.02f, 21.358f);
            path.AddBezier (13.02f, 21.358f, 12.96888107f, 21.53326491f, 13.01735181f, 21.7224586f, 13.14644661f, 21.85155339f);
            path.AddBezier (13.14644661f, 21.85155339f, 13.2755414f, 21.98064819f, 13.46473509f, 22.02911893f, 13.64f, 21.978f);
            path.AddLine (13.64f, 21.978f, 16.51f, 21.141f);
            path.AddBezier (16.51f, 21.141f, 16.83266529f, 21.04677932f, 17.12637825f, 20.87275267f, 17.364f, 20.635f);
            path.AddLine (17.364f, 20.635f, 21.377f, 16.626f);
            path.AddBezier (21.377f, 16.626f, 21.91361293f, 16.08938709f, 22.12318414f, 15.30725672f, 21.92677018f, 14.57422983f);
            path.AddBezier (21.92677018f, 14.57422983f, 21.73035622f, 13.84120295f, 21.15779705f, 13.26864378f, 20.42477017f, 13.07222982f);
            path.AddBezier (20.42477017f, 13.07222982f, 19.69174328f, 12.87581586f, 18.90961291f, 13.08538707f, 18.373f, 13.622f);
            path.CloseFigure ();
            path.StartFigure ();
            path.AddLine (7.0f, 3.0f, 7.0f, 7.0f);
            path.AddBezier (7.0f, 7.0f, 7.0f, 7.55228475f, 7.44771525f, 8.0f, 8.0f, 8.0f);
            path.AddLine (8.0f, 8.0f, 15.0f, 8.0f);
            path.StartFigure ();
            path.AddLine (9.0f, 21.0f, 5.0f, 21.0f);
            path.AddBezier (5.0f, 21.0f, 3.8954305f, 21.0f, 3.0f, 20.1045695f, 3.0f, 19.0f);
            path.AddLine (3.0f, 19.0f, 3.0f, 5.0f);
            path.AddBezier (3.0f, 5.0f, 3.0f, 3.8954305f, 3.8954305f, 3.0f, 5.0f, 3.0f);
            path.AddLine (5.0f, 3.0f, 15.2f, 3.0f);
            path.AddBezier (15.2f, 3.0f, 15.72753853f, 3.0075141f, 16.23073908f, 3.22317147f, 16.6f, 3.6f);
            path.AddLine (16.6f, 3.6f, 20.4f, 7.4f);
            path.AddBezier (20.4f, 7.4f, 20.77682853f, 7.76926092f, 20.9924859f, 8.27246147f, 21.0f, 8.8f);
            path.AddLine (21.0f, 8.8f, 21.0f, 9.1f);
            break;
        case Icon::sun:
            path.StartFigure ();
            path.AddEllipse (8.0f, 8.0f, 8.0f, 8.0f);
            path.StartFigure ();
            path.AddLine (12.0f, 2.0f, 12.0f, 4.0f);
            path.StartFigure ();
            path.AddLine (12.0f, 20.0f, 12.0f, 22.0f);
            path.StartFigure ();
            path.AddLine (4.93f, 4.93f, 6.34f, 6.34f);
            path.StartFigure ();
            path.AddLine (17.66f, 17.66f, 19.07f, 19.07f);
            path.StartFigure ();
            path.AddLine (2.0f, 12.0f, 4.0f, 12.0f);
            path.StartFigure ();
            path.AddLine (20.0f, 12.0f, 22.0f, 12.0f);
            path.StartFigure ();
            path.AddLine (6.34f, 17.66f, 4.93f, 19.07f);
            path.StartFigure ();
            path.AddLine (19.07f, 4.93f, 17.66f, 6.34f);
            break;
        case Icon::moon:
            path.StartFigure ();
            path.AddBezier (20.985f, 12.486f, 20.72385987f, 17.32349573f, 16.6803792f, 21.0863291f, 11.8366198f, 20.99941532f);
            path.AddBezier (11.8366198f, 20.99941532f, 6.9928604f, 20.91250154f, 3.08697563f, 17.0070291f, 2.99955051f, 12.1632789f);
            path.AddBezier (2.99955051f, 12.1632789f, 2.91212538f, 7.3195287f, 6.67453186f, 3.27565082f, 11.512f, 3.014f);
            path.AddBezier (11.512f, 3.014f, 11.917f, 2.992f, 12.129f, 3.474f, 11.914f, 3.817f);
            path.AddBezier (11.914f, 3.817f, 10.4331861f, 6.18625656f, 10.78369681f, 9.26405365f, 12.75932158f, 11.23967842f);
            path.AddBezier (12.75932158f, 11.23967842f, 14.73494635f, 13.21530319f, 17.81274344f, 13.5658139f, 20.182f, 12.085f);
            path.AddBezier (20.182f, 12.085f, 20.526f, 11.87f, 21.007f, 12.081f, 20.985f, 12.486f);
            break;
        case Icon::zoomIn:
            path.StartFigure ();
            path.AddEllipse (3.0f, 3.0f, 16.0f, 16.0f);
            path.StartFigure ();
            path.AddLine (21.0f, 21.0f, 16.65f, 16.65f);
            path.StartFigure ();
            path.AddLine (11.0f, 8.0f, 11.0f, 14.0f);
            path.StartFigure ();
            path.AddLine (8.0f, 11.0f, 14.0f, 11.0f);
            break;
        case Icon::zoomOut:
            path.StartFigure ();
            path.AddEllipse (3.0f, 3.0f, 16.0f, 16.0f);
            path.StartFigure ();
            path.AddLine (21.0f, 21.0f, 16.65f, 16.65f);
            path.StartFigure ();
            path.AddLine (8.0f, 11.0f, 14.0f, 11.0f);
            break;
    }

    graphics.DrawPath (&stroke, &path);
    graphics.Restore (savedState);
}
} // namespace inha::footerIcons
