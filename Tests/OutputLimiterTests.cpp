#include "OutputLimiter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf (stderr, "FAIL line%d: %s\n", __LINE__, message); ++failures; } } while (false)

float previousProtection (float sample)
{
    const auto magnitude = std::abs (sample);
    return magnitude <= 0.98f ? sample
        : std::copysign (0.98f + 0.02f * std::tanh ((magnitude - 0.98f) * 50.0f), sample);
}

struct ToneResult
{
    double residual = 0.0, oldResidual = 0.0, maximum = 0.0, gainVariation = 0.0;
};

ToneResult measureTone (double sampleRate, double frequency)
{
    InhaGrate::OutputLimiter limiter;
    limiter.prepare (sampleRate);
    const auto settle = static_cast<int> (sampleRate * 2.0);
    const auto measured = static_cast<int> (sampleRate * 2.0);
    double sineSine = 0.0, sineCosine = 0.0, cosineCosine = 0.0;
    double outputSine = 0.0, outputCosine = 0.0, energy = 0.0;
    double oldSine = 0.0, oldCosine = 0.0, oldEnergy = 0.0;
    double minimumGain = 1.0, maximumGain = 0.0;
    ToneResult result;
    bool linked = true, bounded = true;
    for (int frame = 0; frame < settle + measured; ++frame)
    {
        const auto phase = 2.0 * pi * frequency * frame / sampleRate + 0.137;
        const auto sine = std::sin (phase), cosine = std::cos (phase);
        const auto input = static_cast<float> (2.4 * sine);
        const auto output = limiter.process ({ input, input * -0.25f });
        bounded = bounded && std::isfinite (output[0]) &&
            std::abs (output[0]) <= InhaGrate::OutputLimiter::ceiling &&
            std::abs (output[1]) <= InhaGrate::OutputLimiter::ceiling;
        linked = linked && output[1] == output[0] * -0.25f;
        if (frame < settle)
            continue;
        const auto old = previousProtection (input);
        sineSine += sine * sine;
        sineCosine += sine * cosine;
        cosineCosine += cosine * cosine;
        outputSine += output[0] * sine;
        outputCosine += output[0] * cosine;
        energy += static_cast<double> (output[0]) * output[0];
        oldSine += old * sine;
        oldCosine += old * cosine;
        oldEnergy += static_cast<double> (old) * old;
        result.maximum = std::max (result.maximum, std::abs (static_cast<double> (output[0])));
        if (std::abs (input) > 0.1f)
        {
            const auto gain = static_cast<double> (output[0]) / input;
            minimumGain = std::min (minimumGain, gain);
            maximumGain = std::max (maximumGain, gain);
        }
    }
    // Least-squares sine/cosine fit also works for non-integer analysis periods.
    const auto determinant = sineSine * cosineCosine - sineCosine * sineCosine;
    const auto fittedEnergy = [&] (double ys, double yc) {
        const auto a = (ys * cosineCosine - yc * sineCosine) / determinant;
        const auto b = (yc * sineSine - ys * sineCosine) / determinant;
        return a * ys + b * yc;
    };
    const auto fundamental = fittedEnergy (outputSine, outputCosine);
    const auto oldFundamental = fittedEnergy (oldSine, oldCosine);
    result.residual = std::sqrt (std::max (0.0, energy - fundamental) / fundamental);
    result.oldResidual = std::sqrt (std::max (0.0, oldEnergy - oldFundamental) / oldFundamental);
    result.gainVariation = maximumGain - minimumGain;
    CHECK (bounded, "overloaded sine stays finite and at or below the0.98 peak ceiling");
    CHECK (linked, "one positive gain preserves both channels and their stereo ratio");
    return result;
}

void testSteadyTones()
{
    for (double sampleRate : { 8000.0, 44100.0, 48000.0, 192000.0 })
    {
        for (double frequency : { 24.0, 437.0, 997.0, 437.13 })
        {
            const auto result = measureTone (sampleRate, frequency);
            CHECK (result.residual < 0.0002, "sustained overloaded tones remain nearly pure sines after the gain settles");
            CHECK (result.oldResidual > 0.10 && result.residual < result.oldResidual * 0.005,
                   "peak gain control removes the strong harmonics made by the old narrow waveshaping knee");
            CHECK (result.gainVariation < 0.0002, "hold refresh prevents periodic gain ripple between sampled sine peaks");
            CHECK (result.maximum > 0.97, "limiter uses available output headroom instead of normalizing the MASS count");
        }
    }
}

void testExactNeutralAndHeadroom()
{
    InhaGrate::OutputLimiter limiter;
    limiter.prepare (48000.0);
    bool exact = true;
    for (int frame = 0; frame < 30000; ++frame)
    {
        // Includes the maximum sum of eight0.1-amplitude MASS layers.
        const auto sine = static_cast<float> (std::sin (frame * 0.019));
        const std::array<float, 2> input {{ sine * 0.4850063f, sine * -0.083f }};
        exact = exact && limiter.process (input) == input;
    }
    CHECK (exact, "below-ceiling MASS gain and independent stereo pass sample-exact without normalization");
    const std::array<float, 2> edge {{ InhaGrate::OutputLimiter::ceiling, -InhaGrate::OutputLimiter::ceiling }};
    CHECK (limiter.process (edge) == edge, "the ceiling sample itself is unchanged");
    const auto rightPeak = limiter.process ({ 0.2f, -2.0f });
    CHECK (std::abs (rightPeak[1]) <= InhaGrate::OutputLimiter::ceiling &&
           std::abs (rightPeak[0] + rightPeak[1] * 0.1f) < 1.0e-7f,
           "a peak in the right channel limits both channels with the same gain");
    limiter.reset();
    const std::array<float, 2> test {{ 0.123456f, -0.333333f }};
    CHECK (limiter.process (test) == test, "reset clears earlier overload attenuation");
}

void testImpulseRecoveryAndSafety()
{
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
    {
        InhaGrate::OutputLimiter limiter;
        limiter.prepare (rate);
        const auto impulse = limiter.process ({ 4.0f, -1.0f });
        CHECK (impulse[0] == InhaGrate::OutputLimiter::ceiling && impulse[1] == impulse[0] * -0.25f,
               "first overload is controlled on the same sample with no lookahead latency");
        float previousGain = impulse[0] / 4.0f;
        bool held = true, smooth = true, monotonic = true;
        float afterOneRelease = 0.0f;
        for (int frame = 0; frame < static_cast<int> (rate * 4.0); ++frame)
        {
            const auto output = limiter.process ({ 0.1f, -0.02f });
            const auto gain = output[0] / 0.1f;
            if (frame < static_cast<int> (rate * 0.039))
                held = held && std::abs (gain - impulse[0] / 4.0f) < 1.0e-7f;
            smooth = smooth && std::abs (gain - previousGain) < 0.001f;
            monotonic = monotonic && gain + 1.0e-7f >= previousGain;
            if (frame == static_cast<int> (rate * 0.160)) afterOneRelease = gain;
            previousGain = gain;
        }
        CHECK (held, "an isolated peak holds its common gain for40ms");
        CHECK (smooth && monotonic && afterOneRelease > 0.71f && afterOneRelease < 0.74f,
               "release is smooth and follows the120ms recovery coefficient");
        const std::array<float, 2> neutral {{ 0.127399f, -0.267901f }};
        CHECK (limiter.process (neutral) == neutral, "recovery eventually returns to sample-exact unity");
        limiter.reset();
        bool finite = true, ceiling = true;
        for (const auto sample : { std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
                                   0.9800001f, 1.0f, 3.0f, 0.0f })
        {
            const auto output = limiter.process ({ sample, -sample });
            finite = finite && std::isfinite (output[0]) && std::isfinite (output[1]);
            ceiling = ceiling && std::abs (output[0]) <= InhaGrate::OutputLimiter::ceiling &&
                std::abs (output[1]) <= InhaGrate::OutputLimiter::ceiling;
        }
        CHECK (finite && ceiling, "float overshoots, huge samples and invalid input remain finite and peak-bounded");
        limiter.reset();
        // A substantially lower overloaded signal must recover rather than
        // retaining the gain chosen for a much louder earlier passage forever.
        limiter.process ({ 4.0f, 0.0f });
        float recovered = 0.0f;
        for (int frame = 0; frame < static_cast<int> (rate * 0.8); ++frame)
            recovered = limiter.process ({ 1.2f, 0.0f })[0];
        CHECK (recovered > 0.97f && recovered <= InhaGrate::OutputLimiter::ceiling,
               "a quieter sustained overload recovers while retaining safe peak headroom");
    }
}
}

int main()
{
    testSteadyTones();
    testExactNeutralAndHeadroom();
    testImpulseRecoveryAndSafety();
    if (failures == 0)
        std::puts ("Output limiter neutral, stereo, low-distortion overload, peak safety and recovery checks passed");
    return failures == 0 ? 0 : 1;
}
