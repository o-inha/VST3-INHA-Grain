#pragma once

#include <algorithm>
#include <cmath>

namespace Inha::Ui
{
struct ModifierKeys
{
    bool shift = false;
    bool control = false;
    bool alt = false;
};

enum class RangeDragMode
{
    handle,
    translate,
    symmetric
};

struct RangeValues
{
    double minimum = 0.0;
    double maximum = 1.0;
};

struct ZeroToggle
{
    double value = 0.0;
    double rememberedValue = 0.0;
};

inline constexpr double normalDragPixels = 160.0;
inline constexpr double fineDragPixels = 600.0;

[[nodiscard]] constexpr RangeDragMode rangeDragMode (ModifierKeys modifiers) noexcept
{
    // Ctrl has priority if Ctrl and Alt are both pressed accidentally.
    if (modifiers.control)
        return RangeDragMode::translate;
    if (modifiers.alt)
        return RangeDragMode::symmetric;
    return RangeDragMode::handle;
}

[[nodiscard]] constexpr double pixelsForFullRange (ModifierKeys modifiers) noexcept
{
    return modifiers.shift ? fineDragPixels : normalDragPixels;
}

[[nodiscard]] inline double dragDelta (double verticalPixels,
                                       ModifierKeys modifiers = {}) noexcept
{
    return verticalPixels / pixelsForFullRange (modifiers);
}

[[nodiscard]] inline double draggedValue (double startValue, double verticalPixels,
                                          ModifierKeys modifiers = {}) noexcept
{
    return std::clamp (startValue + dragDelta (verticalPixels, modifiers), 0.0, 1.0);
}

[[nodiscard]] inline RangeValues translateRange (RangeValues start,
                                                  double verticalPixels,
                                                  ModifierKeys modifiers = {}) noexcept
{
    const auto requested = dragDelta (verticalPixels, modifiers);
    const auto delta = std::clamp (requested, -start.minimum, 1.0 - start.maximum);
    return { start.minimum + delta, start.maximum + delta };
}

[[nodiscard]] inline RangeValues symmetricRange (RangeValues start,
                                                  double verticalPixels,
                                                  ModifierKeys modifiers = {}) noexcept
{
    const auto midpoint = (start.minimum + start.maximum) * 0.5;
    const auto halfWidth = (start.maximum - start.minimum) * 0.5;
    const auto maximumHalfWidth = std::min (midpoint, 1.0 - midpoint);
    const auto nextHalfWidth = std::clamp (
        halfWidth + dragDelta (verticalPixels, modifiers), 0.0, maximumHalfWidth);
    return { midpoint - nextHalfWidth, midpoint + nextHalfWidth };
}

[[nodiscard]] inline double normalizedValue (double value) noexcept
{
    return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
}

[[nodiscard]] inline ZeroToggle toggleZero (double current,
                                             double rememberedValue) noexcept
{
    current = normalizedValue (current);
    rememberedValue = normalizedValue (rememberedValue);
    if (current > 1.0e-9)
        return { 0.0, current };
    return { rememberedValue, rememberedValue };
}

[[nodiscard]] inline ZeroToggle setZero (double current,
                                          double rememberedValue) noexcept
{
    current = normalizedValue (current);
    rememberedValue = normalizedValue (rememberedValue);
    return { 0.0, current > 1.0e-9 ? current : rememberedValue };
}
}
