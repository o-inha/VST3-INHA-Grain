#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace InhaAutomation
{
// Fixed storage and a per-lane binary search keep dense automation bounded.
// Sorting happens once after collection; std::sort never allocates.
template <std::size_t ParameterCount, std::size_t MaximumPoints = 512>
class Timeline
{
public:
    struct Point
    {
        std::size_t parameter = 0;
        int offset = 0;
        double value = 0.0;
        std::size_t insertionOrder = 0;
    };

    void reset (const std::array<double, ParameterCount>& initialValues,
                int newBlockSize) noexcept
    {
        for (std::size_t parameter = 0; parameter < ParameterCount; ++parameter)
            startValues[parameter] = clean (initialValues[parameter]);
        blockSize = std::max (0, newBlockSize);
        pointCount = 0;
        laneHasPoints.fill (false);
        prepared = false;
    }

    [[nodiscard]] bool addPoint (std::size_t parameter, int offset,
                                 double value) noexcept
    {
        if (parameter >= ParameterCount || pointCount >= MaximumPoints)
            return false;
        points[pointCount] = { parameter, std::clamp (offset, 0, blockSize),
                              clean (value), pointCount };
        ++pointCount;
        laneHasPoints[parameter] = true;
        prepared = false;
        return true;
    }

    // The caller chooses its per-lane budget before reading host points. For
    // dense lanes, uniformly retain both endpoints and bounded interior points.
    // Source counts come from VST3's signed 32-bit point count; the product is
    // safely representable on this Windows x64 target.
    [[nodiscard]] static std::size_t sourcePointIndex (
        std::size_t retainedIndex, std::size_t retainedCount,
        std::size_t sourceCount) noexcept
    {
        if (sourceCount == 0 || retainedCount < 2)
            return 0;
        retainedCount = std::min (retainedCount, sourceCount);
        if (retainedCount < 2)
            return 0;
        retainedIndex = std::min (retainedIndex, retainedCount - 1);
        return retainedIndex * (sourceCount - 1) / (retainedCount - 1);
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
        if (! laneHasPoints[parameter])
            return startValues[parameter];
        prepare();
        offset = std::clamp (offset, 0, blockSize);
        const auto begin = points.begin() + static_cast<std::ptrdiff_t> (laneBegin[parameter]);
        const auto end = points.begin() + static_cast<std::ptrdiff_t> (laneBegin[parameter + 1]);
        const auto next = std::upper_bound (begin, end, offset,
            [] (int sample, const Point& point) { return sample < point.offset; });
        auto previousOffset = 0;
        auto previousValue = startValues[parameter];
        if (next != begin)
        {
            const auto& previous = *(next - 1);
            previousOffset = previous.offset;
            previousValue = previous.value;
            if (previousOffset == offset)
                return previousValue;
        }
        if (discrete || next == end || next->offset <= previousOffset)
            return previousValue;
        // Duplicate offsets use the final host point both at the sample and
        // as the approaching interpolation endpoint.
        const auto endpoint = std::upper_bound (next, end, next->offset,
            [] (int sample, const Point& point) { return sample < point.offset; }) - 1;
        const auto proportion = static_cast<double> (offset - previousOffset)
                              / static_cast<double> (endpoint->offset - previousOffset);
        return previousValue + (endpoint->value - previousValue) * proportion;
    }

    template <std::size_t BoundaryCapacity>
    [[nodiscard]] std::size_t boundaries (
        std::array<int, BoundaryCapacity>& result) const noexcept
    {
        static_assert (BoundaryCapacity >= 2);
        prepare();
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
    static double clean (double value) noexcept
    {
        return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
    }
    void prepare() const noexcept
    {
        if (prepared)
            return;
        std::sort (points.begin(), points.begin() + static_cast<std::ptrdiff_t> (pointCount),
            [] (const Point& left, const Point& right)
            {
                if (left.parameter != right.parameter)
                    return left.parameter < right.parameter;
                if (left.offset != right.offset)
                    return left.offset < right.offset;
                return left.insertionOrder < right.insertionOrder;
            });
        std::size_t index = 0;
        for (std::size_t parameter = 0; parameter < ParameterCount; ++parameter)
        {
            laneBegin[parameter] = index;
            while (index < pointCount && points[index].parameter == parameter)
                ++index;
        }
        laneBegin[ParameterCount] = pointCount;
        prepared = true;
    }
    std::array<double, ParameterCount> startValues {};
    mutable std::array<Point, MaximumPoints> points {};
    mutable std::array<std::size_t, ParameterCount + 1> laneBegin {};
    std::array<bool, ParameterCount> laneHasPoints {};
    std::size_t pointCount = 0;
    int blockSize = 0;
    mutable bool prepared = false;
};
}
