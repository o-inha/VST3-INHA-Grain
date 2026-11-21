#pragma once

#include <algorithm>
#include <cmath>

namespace InhaGrate
{
// A stereo-linked, input-keyed wet ducker. The input is taken before the DRY
// output fader; neither wet audio nor event envelopes feed the detector.
class WetDucker
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 384000.0) : 48000.0;
        const auto coefficient = [sampleRate] (double ms) {
            return static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * ms * 0.001)));
        };
        detectorAttack_ = coefficient (0.5);
        detectorRelease_ = coefficient (30.0);
        gainAttack_ = coefficient (3.0);
        gainRelease_ = coefficient (120.0);
        reset();
    }

    void reset() noexcept { envelope_ = 0.0f; gain_ = 1.0f; }

    float process (float left, float right, float percent) noexcept
    {
        const auto safe = [] (float value) { return std::isfinite (value) ? std::abs (value) : 0.0f; };
        const auto level = std::max (safe (left), safe (right));
        envelope_ += (level - envelope_) * (level > envelope_ ? detectorAttack_ : detectorRelease_);
        if (envelope_ < 1.0e-20f) envelope_ = 0.0f;
        const auto depth = std::isfinite (percent) ? std::clamp (percent * 0.01f, 0.0f, 1.0f) : 0.0f;
        // Quiet input below -36 dBFS causes no reduction; at -12 dBFS and
        // above the chosen depth is fully applied. A100% setting nearly
        // mutes the wet path after the short attack, while0% is neutral.
        constexpr float quiet = 0.0158489319f, loud = 0.2511886432f;
        const auto activity = envelope_ <= quiet ? 0.0f : envelope_ >= loud ? 1.0f
            : std::clamp ((20.0f * std::log10 (envelope_) + 36.0f) / 24.0f, 0.0f, 1.0f);
        const auto target = 1.0f - depth * activity;
        const auto speed = target < gain_ || depth == 0.0f ? gainAttack_ : gainRelease_;
        const auto previousGain = gain_;
        gain_ += (target - gain_) * speed;
        if (std::abs (target - gain_) < 1.0e-6f || gain_ == previousGain) gain_ = target;
        return gain_;
    }

private:
    float detectorAttack_ = 1.0f, detectorRelease_ = 1.0f;
    float gainAttack_ = 1.0f, gainRelease_ = 1.0f;
    float envelope_ = 0.0f, gain_ = 1.0f;
};
}
