#pragma once

#include <algorithm>
#include <cmath>

namespace Inha::Parameter
{
enum class CurveType
{
    linear,
    logarithmic,
    power
};

struct Curve
{
    CurveType type = CurveType::linear;
    double exponent = 1.0;
};

[[nodiscard]] inline double clampNormal (double value) noexcept
{
    return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
}

[[nodiscard]] inline double toPlain (double normalized, double minimum, double maximum,
                                     Curve curve = {}) noexcept
{
    const auto value = clampNormal (normalized);
    if (! std::isfinite (minimum) || ! std::isfinite (maximum) || maximum <= minimum)
        return minimum;
    if (curve.type == CurveType::logarithmic && minimum > 0.0)
        return minimum * std::pow (maximum / minimum, value);
    if (curve.type == CurveType::power && curve.exponent > 0.0)
        return minimum + (maximum - minimum) * std::pow (value, curve.exponent);
    return minimum + (maximum - minimum) * value;
}

[[nodiscard]] inline double toNormal (double plain, double minimum, double maximum,
                                      Curve curve = {}) noexcept
{
    if (! std::isfinite (minimum) || ! std::isfinite (maximum) || maximum <= minimum)
        return 0.0;
    const auto value = std::clamp (std::isfinite (plain) ? plain : minimum,
                                   minimum, maximum);
    if (curve.type == CurveType::logarithmic && minimum > 0.0)
        return clampNormal (std::log (value / minimum) / std::log (maximum / minimum));
    const auto linear = (value - minimum) / (maximum - minimum);
    if (curve.type == CurveType::power && curve.exponent > 0.0)
        return clampNormal (std::pow (linear, 1.0 / curve.exponent));
    return clampNormal (linear);
}
}
