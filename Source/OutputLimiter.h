#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace InhaGrate
{
// Zero-lookahead peak gain control, shared by the two channels. Sustained
// overload becomes a quieter waveform instead of a flattened waveform.
class OutputLimiter
{
public:
    static constexpr float ceiling = 0.98f;

    void prepare (double sampleRate) noexcept
    {
        sampleRate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 384000.0) : 48000.0;
        holdFrames_ = static_cast<std::uint32_t> (std::max (1.0, std::round (sampleRate * 0.040)));
        releaseCoefficient_ = 1.0 - std::exp (-1.0 / (sampleRate * 0.120));
        reset();
    }

    void reset() noexcept { gain_ = 1.0; holdRemaining_ = 0; }

    std::array<float, 2> process (std::array<float, 2> input) noexcept
    {
        for (auto& sample : input)
            if (!std::isfinite (sample)) sample = 0.0f;
        const auto peak = std::max (std::abs (static_cast<double> (input[0])),
                                    std::abs (static_cast<double> (input[1])));
        const auto requiredGain = peak > ceiling ? static_cast<double> (ceiling) / peak : 1.0;
        if (requiredGain < gain_)
        {
            gain_ = requiredGain;
            holdRemaining_ = holdFrames_;
        }
        else if (gain_ < 1.0)
        {
            // Refresh near the held peak, rather than only at a new record.
            // Off-grid sine peaks differ slightly from cycle to cycle; without
            // this tolerance the gain could recover between equal loud cycles.
            if (peak > ceiling && peak * gain_ >= static_cast<double> (ceiling) * 0.99)
                holdRemaining_ = holdFrames_;
            else if (holdRemaining_ != 0)
                --holdRemaining_;
            else
            {
                const auto previous = gain_;
                gain_ += (1.0 - gain_) * releaseCoefficient_;
                if (1.0 - gain_ < 1.0e-12 || gain_ == previous) gain_ = 1.0;
                // Release must never produce an overshoot on this sample.
                if (gain_ > requiredGain)
                {
                    gain_ = requiredGain;
                    holdRemaining_ = holdFrames_;
                }
            }
        }
        if (gain_ == 1.0)
            return input;
        for (auto& sample : input)
            sample = static_cast<float> (static_cast<double> (sample) * gain_);
        return input;
    }

private:
    double gain_ = 1.0, releaseCoefficient_ = 0.0001735960415896;
    std::uint32_t holdFrames_ = 1920, holdRemaining_ = 0;
};
}
