#pragma once

#include <cstdint>

namespace Inha::Ui
{
struct Colour
{
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;

    [[nodiscard]] constexpr std::uint32_t nativeColourRef() const noexcept
    {
        // Win32 COLORREF stores RGB as 0x00bbggrr.
        return static_cast<std::uint32_t> (red)
             | (static_cast<std::uint32_t> (green) << 8u)
             | (static_cast<std::uint32_t> (blue) << 16u);
    }
};

namespace Theme
{
// These values come from the current shipping INHA plug-ins. They are the
// runtime source of truth; design documents must mirror the code, not vice versa.
inline constexpr Colour panel { 240, 233, 214 };
inline constexpr Colour ink { 16, 16, 16 };
inline constexpr Colour muted { 122, 117, 112 };
inline constexpr Colour iconMuted { 140, 135, 130 };
inline constexpr Colour line { 201, 190, 158 };
inline constexpr Colour rangeFill { 222, 216, 197 };
inline constexpr Colour disabledInk { 168, 162, 148 };
inline constexpr Colour disabledFill { 230, 224, 207 };
inline constexpr Colour activeGreen { 88, 177, 92 };
inline constexpr Colour activeGlow { 190, 221, 176 };
inline constexpr Colour selection { 53, 50, 43 };
inline constexpr Colour playheadGold { 151, 124, 48 };
}
}
