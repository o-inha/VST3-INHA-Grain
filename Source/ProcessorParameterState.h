#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace InhaAutomation
{
// Processor-owned values never fall back to the controller's display values.
// Only audio-thread automation, explicit local edits and state loads may
// change them. The UI-to-audio mailboxes are bounded and non-blocking.
template <std::size_t ParameterCount>
class ProcessorParameterState
{
public:
    using Values = std::array<double, ParameterCount>;
    using Mask = std::array<bool, ParameterCount>;

    explicit ProcessorParameterState (const Values& initial = {}) noexcept
        : current (initial)
    {
        for (std::size_t id = 0; id < ParameterCount; ++id)
        {
            published[id].store (clean (initial[id]), std::memory_order_relaxed);
            edits[id].store (none, std::memory_order_relaxed);
            restored[id].store (none, std::memory_order_relaxed);
            current[id] = clean (initial[id]);
        }
    }

    void queueEdit (std::size_t id, double value) noexcept
    {
        if (id < ParameterCount)
            edits[id].store (clean (value), std::memory_order_release);
    }

    void restore (const Values& values) noexcept
    {
        for (std::size_t id = 0; id < ParameterCount; ++id)
        {
            // Loading a preset supersedes any previously queued local gesture.
            edits[id].store (none, std::memory_order_release);
            restored[id].store (clean (values[id]), std::memory_order_release);
        }
    }

    // Called only by the processor (or while processing is suspended).
    [[nodiscard]] const Values& consumePending (const Mask& automated = {}) noexcept
    {
        for (std::size_t id = 0; id < ParameterCount; ++id)
        {
            const auto state = restored[id].exchange (none, std::memory_order_acq_rel);
            if (state >= 0.0)
                current[id] = state;
            const auto edit = edits[id].exchange (none, std::memory_order_acq_rel);
            // If the host delivers the gesture itself, preserve its offset.
            // Do not also apply the local echo at the start of that block.
            if (edit >= 0.0 && ! automated[id])
                current[id] = edit;
        }
        return current;
    }

    void commit (const Values& values) noexcept
    {
        current = values;
        publication.fetch_add (1, std::memory_order_acq_rel);
        for (std::size_t id = 0; id < ParameterCount; ++id)
            published[id].store (current[id], std::memory_order_release);
        publication.fetch_add (1, std::memory_order_release);
    }

    // One bounded attempt on the UI thread. If publication overlaps a frame,
    // retain the preceding frame instead of displaying mixed min/max values.
    [[nodiscard]] bool snapshotForDisplay (Values& result) const noexcept
    {
        const auto before = publication.load (std::memory_order_acquire);
        if ((before & 1u) != 0u)
            return false;
        Values snapshot {};
        for (std::size_t id = 0; id < ParameterCount; ++id)
            snapshot[id] = valueForState (id);
        if (publication.load (std::memory_order_acquire) != before)
            return false;
        result = snapshot;
        return true;
    }

    [[nodiscard]] const Values& currentValues() const noexcept { return current; }

    // Project/preset saving must not save an old GUI automation notification.
    // Include genuine edits/state loads even if the audio thread is stopped.
    [[nodiscard]] double valueForState (std::size_t id) const noexcept
    {
        if (id >= ParameterCount)
            return 0.0;
        const auto edit = edits[id].load (std::memory_order_acquire);
        if (edit >= 0.0)
            return edit;
        const auto state = restored[id].load (std::memory_order_acquire);
        return state >= 0.0 ? state
            : published[id].load (std::memory_order_acquire);
    }

private:
    static constexpr double none = -1.0;
    static double clean (double value) noexcept
    {
        return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
    }

    Values current {};
    std::atomic<unsigned> publication { 0 };
    std::array<std::atomic<double>, ParameterCount> published;
    std::array<std::atomic<double>, ParameterCount> edits;
    std::array<std::atomic<double>, ParameterCount> restored;
};
}
