#include "WetEqualizer.h"

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

double response (float low, float high, double frequency, double sampleRate = 48000.0)
{
    InhaGrate::WetEqualizer equalizer;
    equalizer.prepare (sampleRate);
    equalizer.setCutoffs (low, high);
    equalizer.reset();
    double inputEnergy = 0.0, outputEnergy = 0.0;
    const auto frames = static_cast<int> (sampleRate);
    for (int frame = 0; frame < frames * 2; ++frame)
    {
        const auto sample = static_cast<float> (0.2 * std::sin (2.0 * pi * frequency * frame / sampleRate));
        const auto output = equalizer.process ({ sample, 0.0f });
        CHECK (output[1] == 0.0f, "wet EQ never crosses left audio into the right channel");
        if (frame >= frames)
        {
            inputEnergy += static_cast<double> (sample) * sample;
            outputEnergy += static_cast<double> (output[0]) * output[0];
        }
    }
    return std::sqrt (outputEnergy / inputEnergy);
}

void testNeutralAndReset()
{
    InhaGrate::WetEqualizer equalizer;
    equalizer.prepare (48000.0);
    bool exact = true;
    for (int frame = 0; frame < 16000; ++frame)
    {
        const std::array<float, 2> input {{ static_cast<float> (std::sin (frame * 0.019) * 1.7),
                                          static_cast<float> (std::cos (frame * 0.073) * 0.31) }};
        exact = exact && equalizer.process (input) == input;
    }
    CHECK (exact, "full EQ range is sample-exact bypass, preserving independent stereo and interstage peaks");
    equalizer.setCutoffs (300.0f, 3000.0f);
    for (int frame = 0; frame < 10000; ++frame)
        equalizer.process ({ 0.1f, -0.03f });
    equalizer.reset();
    bool silence = true;
    for (int frame = 0; frame < 2000; ++frame)
        silence = silence && equalizer.process ({ 0.0f, 0.0f }) == std::array<float, 2> {{ 0.0f, 0.0f }};
    CHECK (silence, "reset clears EQ history without leaking a previous event/transport tail");
    equalizer.setCutoffs (20.0f, 20000.0f);
    for (int frame = 0; frame < 16000; ++frame)
        equalizer.process ({ 0.1f, -0.02f });
    const std::array<float, 2> test {{ 0.123456f, -0.333333f }};
    CHECK (equalizer.process (test) == test, "restoring both endpoints reaches exact bypass after smooth recovery");
}

void testFrequencyResponse()
{
    const auto lowPassCorner = response (20.0f, 1000.0f, 1000.0);
    const auto highPassCorner = response (1000.0f, 20000.0f, 1000.0);
    CHECK (std::abs (20.0 * std::log10 (lowPassCorner) + 3.0103) < 0.025,
           "low-pass Butterworth corner is approximately minus3dB");
    CHECK (std::abs (20.0 * std::log10 (highPassCorner) + 3.0103) < 0.025,
           "high-pass Butterworth corner is approximately minus3dB");
    const auto lowPassOctave = 20.0 * std::log10 (
        response (20.0f, 100.0f, 1000.0) / response (20.0f, 100.0f, 500.0));
    const auto highPassOctave = 20.0 * std::log10 (
        response (2000.0f, 20000.0f, 100.0) / response (2000.0f, 20000.0f, 200.0));
    CHECK (std::abs (lowPassOctave + 12.0412) < 0.12,
           "low-pass stopband falls approximately12dB per octave");
    CHECK (std::abs (highPassOctave + 12.0412) < 0.12,
           "high-pass stopband falls approximately12dB per octave");
    CHECK (response (20.0f, 1000.0f, 100.0) > 0.999,
           "low-pass leaves a distant passband at unity");
    CHECK (response (1000.0f, 20000.0f, 10000.0) > 0.999,
           "high-pass leaves a distant passband at unity");
}

void testSweepsAndLowRates()
{
    for (const double sampleRate : { 8000.0, 44100.0, 48000.0, 192000.0 })
    {
        InhaGrate::WetEqualizer equalizer;
        equalizer.prepare (sampleRate);
        bool finite = true, stereo = true;
        double largestAddedStep = 0.0;
        std::array<float, 2> previousInput {}, previousOutput {};
        for (int frame = 0; frame < static_cast<int> (sampleRate * 3.0); ++frame)
        {
            if (frame % static_cast<int> (sampleRate * 0.15) == 0)
            {
                const auto selection = static_cast<int> (frame / static_cast<int> (sampleRate * 0.15)) % 5;
                switch (selection)
                {
                    case 0: equalizer.setCutoffs (20.0f, 20000.0f); break;
                    case 1: equalizer.setCutoffs (1000.0f, 1000.0f); break;
                    case 2: equalizer.setCutoffs (19999.0f, 20000.0f); break;
                    case 3: equalizer.setCutoffs (20.0f, 21.0f); break;
                    default: equalizer.setCutoffs (200.0f, 6000.0f); break;
                }
            }
            const auto sample = static_cast<float> (0.1 * std::sin (2.0 * pi * 179.0 * frame / sampleRate));
            const std::array<float, 2> input {{ sample, sample * -0.25f }};
            const auto output = equalizer.process (input);
            finite = finite && std::isfinite (output[0]) && std::isfinite (output[1]) &&
                std::abs (output[0]) < 0.5f && std::abs (output[1]) < 0.5f;
            stereo = stereo && std::abs (output[1] + output[0] * 0.25f) < 1.0e-7f;
            largestAddedStep = std::max (largestAddedStep, std::abs (static_cast<double> (
                (output[0] - previousOutput[0]) - (input[0] - previousInput[0]))));
            previousInput = input;
            previousOutput = output;
        }
        CHECK (finite, "extreme EQ sweeps, equal cutoffs and Nyquist clamping remain finite and bounded");
        CHECK (stereo, "linked EQ coefficients preserve stereo balance through all automation");
        CHECK (largestAddedStep < 0.05, "EQ cutoff and bypass automation avoid discontinuous filter changes");
    }
    InhaGrate::WetEqualizer equalizer;
    equalizer.prepare (std::numeric_limits<double>::quiet_NaN());
    equalizer.setCutoffs (std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    const auto output = equalizer.process ({ std::numeric_limits<float>::quiet_NaN(), 0.25f });
    CHECK (output[0] == 0.0f && output[1] == 0.25f, "invalid settings/input cannot poison the equalizer state");
}
}

int main()
{
    testNeutralAndReset();
    testFrequencyResponse();
    testSweepsAndLowRates();
    if (failures == 0)
        std::puts ("Wet EQ exact bypass, stereo,12dB/oct response, automation and reset checks passed");
    return failures == 0 ? 0 : 1;
}
