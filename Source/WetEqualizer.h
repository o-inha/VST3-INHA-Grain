#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace InhaGrate
{
// Two linked-coefficient, independent-channel Butterworth sections. The range
// endpoints deliberately mean bypass, rather than inaudible filters at 20 Hz
// and 20 kHz. Cutoffs are fixed values; no random choices happen here.
class WetEqualizer
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::clamp (std::isfinite (sampleRate) ? sampleRate : 48000.0,
                                  8000.0, 384000.0);
        frequencySmoothing_ = 1.0 - std::exp (-1.0 / (0.010 * sampleRate_));
        mixSmoothing_ = 1.0 - std::exp (-1.0 / (0.006 * sampleRate_));
        dirty_ = true;
        reset();
    }

    void reset() noexcept
    {
        updateTargets();
        highPass_.clear();
        lowPass_.clear();
        highPass_.setG (targetHighPassG_);
        lowPass_.setG (targetLowPassG_);
        highPassMix_ = targetHighPassMix_;
        lowPassMix_ = targetLowPassMix_;
        controlCountdown_ = 0;
    }

    void setCutoffs (float low, float high) noexcept
    {
        const auto lower = std::clamp (std::isfinite (low) ? low : 20.0f, 20.0f, 20000.0f);
        const auto upper = std::clamp (std::isfinite (high) ? high : 20000.0f, 20.0f, 20000.0f);
        const auto nextLow = std::min (lower, upper);
        const auto nextHigh = std::max (lower, upper);
        if (nextLow != lowHz_ || nextHigh != highHz_)
        {
            lowHz_ = nextLow;
            highHz_ = nextHigh;
            dirty_ = true;
        }
    }

    std::array<float, 2> process (std::array<float, 2> input) noexcept
    {
        // Sample-accurate parameter streams need not run trigonometry for
        // every sample. This control interval is <=2 ms at the lowest rate.
        if (controlCountdown_ == 0)
        {
            updateTargets();
            controlCountdown_ = 16;
        }
        --controlCountdown_;
        highPass_.setG (approach (highPass_.g, targetHighPassG_, frequencySmoothing_));
        lowPass_.setG (approach (lowPass_.g, targetLowPassG_, frequencySmoothing_));
        highPassMix_ = approach (highPassMix_, targetHighPassMix_, mixSmoothing_);
        lowPassMix_ = approach (lowPassMix_, targetLowPassMix_, mixSmoothing_);

        for (std::size_t channel = 0; channel < input.size(); ++channel)
        {
            const double original = std::isfinite (input[channel]) ? input[channel] : 0.0;
            // Keep both sections warm during bypass; activation then crossfades
            // from the current signal instead of introducing an empty-state step.
            const auto highPassed = highPass_.process (original, channel, true);
            const auto afterHighPass = highPassMix_ == 0.0 ? original
                : original + highPassMix_ * (highPassed - original);
            const auto lowPassed = lowPass_.process (afterHighPass, channel, false);
            const auto result = lowPassMix_ == 0.0 ? afterHighPass
                : afterHighPass + lowPassMix_ * (lowPassed - afterHighPass);
            input[channel] = static_cast<float> (result);
        }
        return input;
    }

private:
    struct Section
    {
        double g = 0.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
        std::array<double, 2> integrator1 {}, integrator2 {};
        static constexpr double damping = 1.4142135623730950488;

        void clear() noexcept { integrator1.fill (0.0); integrator2.fill (0.0); }
        void setG (double value) noexcept
        {
            if (g == value)
                return;
            g = value;
            a1 = 1.0 / (1.0 + g * (g + damping));
            a2 = g * a1;
            a3 = g * a2;
        }
        double process (double input, std::size_t channel, bool highPass) noexcept
        {
            const auto difference = input - integrator2[channel];
            const auto band = a1 * integrator1[channel] + a2 * difference;
            const auto low = integrator2[channel] + a2 * integrator1[channel] + a3 * difference;
            integrator1[channel] = 2.0 * band - integrator1[channel];
            integrator2[channel] = 2.0 * low - integrator2[channel];
            // Silence should not leave subnormal integrator tails consuming CPU.
            if (std::abs (integrator1[channel]) < 1.0e-30) integrator1[channel] = 0.0;
            if (std::abs (integrator2[channel]) < 1.0e-30) integrator2[channel] = 0.0;
            return highPass ? input - damping * band - low : low;
        }
    };

    static double approach (double current, double target, double coefficient) noexcept
    {
        if (std::abs (target - current) <= 1.0e-10 * std::max (1.0, std::abs (target)))
            return target;
        return current + (target - current) * coefficient;
    }
    void updateTargets() noexcept
    {
        if (!dirty_)
            return;
        constexpr double pi = 3.1415926535897932384626433832795;
        // Frequencies beyond Nyquist cannot be represented at low host rates.
        // A positive, bounded TPT coefficient remains stable during all sweeps.
        const auto maximum = sampleRate_ * 0.49;
        targetHighPassG_ = std::tan (pi * std::min<double> (lowHz_, maximum) / sampleRate_);
        targetLowPassG_ = std::tan (pi * std::min<double> (highHz_, maximum) / sampleRate_);
        targetHighPassMix_ = lowHz_ <= 20.0f ? 0.0 : 1.0;
        targetLowPassMix_ = highHz_ >= 20000.0f ? 0.0 : 1.0;
        dirty_ = false;
    }

    Section highPass_, lowPass_;
    double sampleRate_ = 48000.0;
    double frequencySmoothing_ = 0.002081164700700744;
    double mixSmoothing_ = 0.0034662010296308616;
    double targetHighPassG_ = 0.0013089976868137788, targetLowPassG_ = 3.732050807568877;
    double highPassMix_ = 0.0, lowPassMix_ = 0.0;
    double targetHighPassMix_ = 0.0, targetLowPassMix_ = 0.0;
    float lowHz_ = 20.0f, highHz_ = 20000.0f;
    unsigned controlCountdown_ = 0;
    bool dirty_ = true;
};
}
