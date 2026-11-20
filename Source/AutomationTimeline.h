#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace InhaAutomation
{
template <std::size_t ParameterCount, std::size_t MaximumPoints = 512>
class Timeline
{
public:
    struct Point
    {
        std::size_t parameter = 0;
        int offset = 0;
        double value = 0.0;
    };

    void reset (const std::array<double, ParameterCount>& initialValues,
                int newBlockSize) noexcept
    {
        startValues = initialValues;
        blockSize = std::max (0, newBlockSize);
        pointCount = 0;
        laneHasPoints.fill (false);
    }

    [[nodiscard]] bool addPoint (std::size_t parameter, int offset,
                                 double value) noexcept
    {
        if (parameter >= ParameterCount || pointCount >= MaximumPoints)
            return false;
        points[pointCount++] = {
            parameter,
            std::clamp (offset, 0, blockSize),
            std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0)
        };
        laneHasPoints[parameter] = true;
        return true;
    }

    [[nodiscard]] bool hasPoints (std::size_t parameter) const noexcept
    {
        return parameter < ParameterCount && laneHasPoints[parameter];
    }

    [[nodiscard]] double valueAt (std::size_t parameter, int offset,
                                  bool discrete) const noexcept
    {
        if (parameter >= ParameterCount)
            return 0.0;
        offset = std::clamp (offset, 0, blockSize);
        auto previousOffset = 0;
        auto previousValue = startValues[parameter];
        auto nextOffset = blockSize + 1;
        auto nextValue = previousValue;
        auto exactFound = false;
        auto exactValue = previousValue;

        for (std::size_t index = 0; index < pointCount; ++index)
        {
            const auto& point = points[index];
            if (point.parameter != parameter)
                continue;
            if (point.offset == offset)
            {
                exactFound = true;
                exactValue = point.value;
            }
            else if (point.offset < offset && point.offset >= previousOffset)
            {
                previousOffset = point.offset;
                previousValue = point.value;
            }
            else if (point.offset > offset && point.offset < nextOffset)
            {
                nextOffset = point.offset;
                nextValue = point.value;
            }
        }

        if (exactFound)
            return exactValue;
        if (discrete || nextOffset > blockSize || nextOffset <= previousOffset)
            return previousValue;
        const auto proportion = static_cast<double> (offset - previousOffset)
                              / static_cast<double> (nextOffset - previousOffset);
        return previousValue + (nextValue - previousValue) * proportion;
    }

    template <std::size_t BoundaryCapacity>
    [[nodiscard]] std::size_t boundaries (
        std::array<int, BoundaryCapacity>& result) const noexcept
    {
        static_assert (BoundaryCapacity >= 2);
        auto count = std::size_t { 0 };
        result[count++] = 0;
        for (std::size_t index = 0; index < pointCount && count < BoundaryCapacity - 1;
             ++index)
        {
            const auto offset = points[index].offset;
            if (offset > 0 && offset < blockSize)
                result[count++] = offset;
        }
        result[count++] = blockSize;
        std::sort (result.begin(), result.begin() + static_cast<std::ptrdiff_t> (count));
        return static_cast<std::size_t> (std::unique (
            result.begin(), result.begin() + static_cast<std::ptrdiff_t> (count))
            - result.begin());
    }

private:
    std::array<double, ParameterCount> startValues {};
    std::array<Point, MaximumPoints> points {};
    std::array<bool, ParameterCount> laneHasPoints {};
    std::size_t pointCount = 0;
    int blockSize = 0;
};
}
