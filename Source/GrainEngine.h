#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace InhaGrain
{
struct Parameters
{
    int mode = 0; // NONE, TRANSIENTS, MIDI NOTES, VOLUME
    float amount = 100.0f;
    float sensitivity = 50.0f;
    float thresholdDb = -24.0f;
    float scatterMs = 2000.0f;
    float grainMinMs = 200.0f, grainMaxMs = 1000.0f;
    int mass = 1;
    float panMinPercent = 0.0f, panMaxPercent = 0.0f;
    // The adapter expands these bounds for tempo-synchronized durations.
    float durationMinimumMs = 40.0f, durationMaximumMs = 6000.0f;
    bool synced = false;
    bool bufferTriplets = false, grainTriplets = false;
    float tempoBpm = 120.0f;
    float envMinPercent = 0.0f, envMaxPercent = 50.0f;
    float undulationMinPercent = 0.0f, undulationMaxPercent = 50.0f;
    float speedMinPercent = 50.0f, speedMaxPercent = 200.0f;
    float attackMinMs = 500.0f, attackMaxMs = 1000.0f;
    float releaseMinMs = 800.0f, releaseMaxMs = 6000.0f;
    float duckDryPercent = 90.0f, duckPreviousPercent = 90.0f;
    float dryDb = 0.0f, wetDb = 0.0f;
    std::uint32_t seed = 254743703u;
    bool bypass = false;
    int motionMode = 0; // UNDULATION, SWAY; each mode retains its own ranges.
    float swayAmountMinPercent = 10.0f, swayAmountMaxPercent = 30.0f;
    float swaySpeedMinMs = 500.0f, swaySpeedMaxMs = 1500.0f;
};

// No SDK, locks, allocation, file access, or bulk copies on the audio path.
// prepare() is the only allocation point; reset() invalidates capture history
// by changing counters, without clearing the (potentially large) buffers.
class GrainEngine
{
public:
    static constexpr std::size_t maxEvents = 6;
    static constexpr std::size_t maxStreams = 8;
    static constexpr std::size_t maxGrainsPerEvent = 64;

    void prepare (double sampleRate);
    void reset (std::int64_t timelineSample = 0) noexcept;
    void setParameters (const Parameters&) noexcept;
    void noteOn() noexcept;
    std::array<float, 2> process (float left, float right) noexcept;
    bool isActive() const noexcept { return activity_ * (1.0f - bypassMix_) > 0.00001f; }
    float bypassMix() const noexcept { return bypassMix_; }
    std::size_t memoryBytes() const noexcept;
    std::size_t activeEvents() const noexcept;

private:
    friend struct GrainEngineTestAccess;
    struct Grain
    {
        bool active = false;
        std::uint32_t age = 0, duration = 0;
        std::uint64_t begin = 0;
        std::uint32_t position = 0;
        float leftGain = 1.0f, rightGain = 1.0f;
        std::uint16_t curve = 0;
        float curveMix = 0.0f;
    };
    struct Event
    {
        bool active = false, live = false, held = false;
        bool releasing = false, retiring = false;
        std::uint64_t key = 0, written = 0, grainSerial = 0;
        std::uint64_t age = 0, attack = 1, release = 1, releaseAge = 0;
        std::uint64_t captureLimit = 1;
        std::array<std::uint32_t, maxStreams> previousGrain {};
        std::array<std::int64_t, maxStreams> nextGrain {};
        std::uint64_t audioStart = 0;
        bool audioStarted = false;
        float releaseStart = 1.0f, envelope = 0.0f;
        float undulation = 0.0f, speed = 1.0f;
        float grainQuantile = 0.0f;
        float pressureGain = 1.0f;
        std::uint32_t pressureRemaining = 0, pressureTotal = 1;
        std::array<Grain, maxGrainsPerEvent> grains {};
        std::vector<float> samples;
    };

    static constexpr std::size_t curveCount = 33, curveSize = 1025;
    std::array<Event, maxEvents> events_ {};
    std::array<std::array<float, curveSize>, curveCount> windows_ {};
    Parameters parameters_ {};
    double sampleRate_ = 48000.0;
    std::size_t capacity_ = 0;
    std::int64_t timeline_ = 0, triggerLockUntil_ = 0;
    std::uint32_t pendingNotes_ = 0;
    float fast_ = 0.0f, slow_ = 0.0f, volume_ = 0.0f;
    float fastAttack_ = 0.0f, fastRelease_ = 0.0f, slowAttack_ = 0.0f;
    float slowRelease_ = 0.0f, volumeAttack_ = 0.0f, volumeRelease_ = 0.0f;
    float gainAttack_ = 0.0f, gainRelease_ = 0.0f;
    float dryGain_ = 1.0f, wetGain_ = 1.0f;
    float dryTarget_ = 1.0f, wetTarget_ = 1.0f, threshold_ = 0.0630957f;
    float dryDuck_ = 1.0f, activity_ = 0.0f, wetBudgetGain_ = 1.0f;
    float bypassMix_ = 0.0f;
    bool transientHigh_ = false, volumeHigh_ = false;
    int currentMode_ = 0;

    static std::uint64_t hash (std::uint64_t) noexcept;
    float random (std::uint64_t key, std::uint64_t domain) const noexcept;
    float pick (float minimum, float maximum, std::uint64_t key,
                std::uint64_t domain) const noexcept;
    std::uint64_t samplesForMs (double ms) const noexcept;
    double swayKnotTime (std::int64_t index) const noexcept;
    float swayValue (std::int64_t absoluteSample) const noexcept;
    void retire (Event&, double milliseconds) noexcept;
    Event* startEvent (bool live, bool held, std::uint64_t ordinal) noexcept;
    void startRelease (Event&) noexcept;
    void launchGrain (Event&, std::size_t stream) noexcept;
    std::array<float, 2> renderEvent (Event&) noexcept;
    float grainWindow (const Grain&) const noexcept;
    float readSample (const Event&, std::uint64_t frame, unsigned channel) const noexcept;
    static float protect (float) noexcept;
};
}
