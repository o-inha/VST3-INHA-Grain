#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace InhaGrain::Timing
{
inline constexpr double minimumMilliseconds = 40.0;
inline constexpr double maximumMilliseconds = 6000.0;
inline constexpr double minimumTempo = 20.0;
inline constexpr double maximumTempo = 999.0;
inline constexpr double fallbackTempo = 120.0;
inline constexpr double maximumCaptureMilliseconds = 24000.0;
// Standard note lengths in quarter-note beats; the last two assume 4/4.
inline constexpr std::array<double, 6> beats {{0.25, 0.5, 1.0, 2.0, 4.0, 8.0}};
inline constexpr std::array<const wchar_t*, 6> labels {{
    L"1/16", L"1/8", L"1/4", L"1/2", L"1 BAR", L"2 BARS"
}};

inline double tempo (double bpm) noexcept
{
    return std::clamp (std::isfinite (bpm) && bpm > 0.0 ? bpm : fallbackTempo,
                       minimumTempo, maximumTempo);
}
inline std::size_t syncIndex (double normalized) noexcept
{
    const auto value = std::clamp (std::isfinite (normalized) ? normalized : 0.0, 0.0, 1.0);
    return static_cast<std::size_t> (std::lround (value * (beats.size() - 1)));
}
inline double syncNormalized (std::size_t index) noexcept
{
    return static_cast<double> (std::min (index, beats.size() - 1)) / (beats.size() - 1);
}
inline double syncMilliseconds (double normalized, double bpm) noexcept
{
    return beats[syncIndex (normalized)] * 60000.0 / tempo (bpm);
}
inline const wchar_t* syncLabel (double normalized) noexcept
{
    return labels[syncIndex (normalized)];
}
inline double quantizeMilliseconds (double milliseconds, double bpm) noexcept
{
    const auto beatMilliseconds = 60000.0 / tempo (bpm);
    const auto wanted = std::isfinite (milliseconds) ? milliseconds : beats.front() * beatMilliseconds;
    std::size_t best = 0;
    for (std::size_t i = 1; i < beats.size(); ++i)
        if (std::abs (wanted - beats[i] * beatMilliseconds) <
            std::abs (wanted - beats[best] * beatMilliseconds)) best = i;
    return beats[best] * beatMilliseconds;
}
inline double floorMilliseconds (double milliseconds, double bpm) noexcept
{
    const auto beatMilliseconds = 60000.0 / tempo (bpm);
    auto result = beats.front() * beatMilliseconds;
    for (const auto beat : beats)
        if (beat * beatMilliseconds <= milliseconds + 1.0e-6)
            result = beat * beatMilliseconds;
    return result;
}
inline double syncNormalizedFromLabel (const std::wstring& label) noexcept
{
    for (std::size_t i = 0; i < labels.size(); ++i)
        if (label == labels[i]) return syncNormalized (i);
    if (label == L"1/1") return syncNormalized (4);
    if (label == L"2/1") return syncNormalized (5);
    return -1.0;
}
inline double minimumDuration (bool synced, double bpm) noexcept
{
    return synced ? syncMilliseconds (0.0, bpm) : minimumMilliseconds;
}
inline double maximumDuration (bool synced, double bpm) noexcept
{
    return synced ? syncMilliseconds (1.0, bpm) : maximumMilliseconds;
}
}
