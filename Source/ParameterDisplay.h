#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace InhaAutomation
{
// UI-thread only. Interpolate presentation, never the host/audio parameter.
// A frame uses one cached value per parameter for its handle, arc and label.
template <std::size_t ParameterCount>
class ParameterDisplay
{
public:
    using Values = std::array<double, ParameterCount>;
    using Mask = std::array<bool, ParameterCount>;

    bool update (const Values& targets, const Mask& immediate,
                 double elapsedSeconds, bool reset = false) noexcept
    {
        const auto alpha = 1.0 - std::exp (
            -std::clamp (elapsedSeconds, 0.0, 0.25) / 0.025);
        auto changed = false;
        for (std::size_t id = 0; id < ParameterCount; ++id)
        {
            const auto target = std::clamp (
                std::isfinite (targets[id]) ? targets[id] : 0.0, 0.0, 1.0);
            const auto previous = displayed[id];
            if (! initialized || reset || immediate[id])
                displayed[id] = target;
            else
            {
                displayed[id] += alpha * (target - displayed[id]);
                if (std::abs (target - displayed[id]) < 1.0e-5)
                    displayed[id] = target;
            }
            changed |= previous != displayed[id];
        }
        initialized = true;
        return changed;
    }

    [[nodiscard]] const Values& values() const noexcept { return displayed; }

private:
    Values displayed {};
    bool initialized = false;
};
}
