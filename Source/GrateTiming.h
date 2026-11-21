#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace InhaGrate::Timing
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

// Straight steps retain their original normalized positions: i/5 == 2i/10.
// Odd positions add triplets without changing stored straight-note requests.
inline constexpr std::array<double, 11> tripletBeats {{
    0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0,
    2.0, 8.0 / 3.0, 4.0, 16.0 / 3.0, 8.0
}};
inline constexpr std::array<const wchar_t*, 11> tripletLabels {{
    L"1/16", L"1/8T", L"1/8", L"1/4T", L"1/4", L"1/2T",
    L"1/2", L"1/1T", L"1 BAR", L"2/1T", L"2 BARS"
}};

inline std::size_t choiceCount (bool allowTriplets = false) noexcept
{
    return allowTriplets ? tripletBeats.size() : beats.size();
}
inline double choiceBeats (std::size_t index, bool allowTriplets = false) noexcept
{
    index = std::min (index, choiceCount (allowTriplets) - 1);
    return allowTriplets ? tripletBeats[index] : beats[index];
}
inline double tempo (double bpm) noexcept
{
    return std::clamp (std::isfinite (bpm) && bpm > 0.0 ? bpm : fallbackTempo,
                       minimumTempo, maximumTempo);
}
inline std::size_t syncIndex (double normalized, bool allowTriplets = false) noexcept
{
    const auto value = std::clamp (std::isfinite (normalized) ? normalized : 0.0, 0.0, 1.0);
    return static_cast<std::size_t> (std::lround (value * (choiceCount (allowTriplets) - 1)));
}
inline double syncNormalized (std::size_t index, bool allowTriplets = false) noexcept
{
    const auto last = choiceCount (allowTriplets) - 1;
    return static_cast<double> (std::min (index, last)) / last;
}
inline double syncMilliseconds (double normalized, double bpm, bool allowTriplets = false) noexcept
{
    return choiceBeats (syncIndex (normalized, allowTriplets), allowTriplets) * 60000.0 / tempo (bpm);
}
inline const wchar_t* syncLabel (double normalized, bool allowTriplets = false) noexcept
{
    const auto index = syncIndex (normalized, allowTriplets);
    return allowTriplets ? tripletLabels[index] : labels[index];
}
inline double quantizeMilliseconds (double milliseconds, double bpm, bool allowTriplets = false) noexcept
{
    const auto beatMilliseconds = 60000.0 / tempo (bpm);
    const auto wanted = std::isfinite (milliseconds) ? milliseconds : choiceBeats (0, allowTriplets) * beatMilliseconds;
    std::size_t best = 0;
    for (std::size_t i = 1; i < choiceCount (allowTriplets); ++i)
        if (std::abs (wanted - choiceBeats (i, allowTriplets) * beatMilliseconds) <
            std::abs (wanted - choiceBeats (best, allowTriplets) * beatMilliseconds)) best = i;
    return choiceBeats (best, allowTriplets) * beatMilliseconds;
}
inline double floorMilliseconds (double milliseconds, double bpm, bool allowTriplets = false) noexcept
{
    const auto beatMilliseconds = 60000.0 / tempo (bpm);
    auto result = choiceBeats (0, allowTriplets) * beatMilliseconds;
    for (std::size_t i = 0; i < choiceCount (allowTriplets); ++i)
    {
        const auto duration = choiceBeats (i, allowTriplets) * beatMilliseconds;
        if (duration <= milliseconds + 1.0e-6)
            result = duration;
    }
    return result;
}
inline double syncNormalizedFromLabel (const std::wstring& label, bool allowTriplets = false) noexcept
{
    for (std::size_t i = 0; i < choiceCount (allowTriplets); ++i)
        if (label == (allowTriplets ? tripletLabels[i] : labels[i]))
            return syncNormalized (i, allowTriplets);
    if (label == L"1/1") return syncNormalized (allowTriplets ? 8 : 4, allowTriplets);
    if (label == L"2/1") return syncNormalized (choiceCount (allowTriplets) - 1, allowTriplets);
    return -1.0;
}
inline double minimumDuration (bool synced, double bpm, bool allowTriplets = false) noexcept
{
    return synced ? syncMilliseconds (0.0, bpm, allowTriplets) : minimumMilliseconds;
}
inline double maximumDuration (bool synced, double bpm, bool allowTriplets = false) noexcept
{
    return synced ? syncMilliseconds (1.0, bpm, allowTriplets) : maximumMilliseconds;
}
}
