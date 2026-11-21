#include "GrateEngine.h"
#include "GrateTiming.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace InhaGrate
{
namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
float finiteClamp (float value, float low, float high, float fallback) noexcept
{
    return std::clamp (std::isfinite (value) ? value : fallback, low, high);
}
float smoothstep (float x) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
float coefficient (double milliseconds, double rate)
{
    return static_cast<float> (1.0 - std::exp (-1.0 / (milliseconds * rate * 0.001)));
}
float dbGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
}
}

std::uint64_t GrateEngine::hash (std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

float GrateEngine::random (std::uint64_t key, std::uint64_t domain) const noexcept
{
    const auto value = hash (key ^ hash (domain) ^
                             (static_cast<std::uint64_t> (parameters_.seed) << 17));
    return static_cast<float> (value >> 40) * (1.0f / 16777216.0f);
}

float GrateEngine::pick (float minimum, float maximum, std::uint64_t key,
                        std::uint64_t domain) const noexcept
{
    if (minimum > maximum)
        std::swap (minimum, maximum);
    return minimum + (maximum - minimum) * random (key, domain);
}

std::uint64_t GrateEngine::samplesForMs (double ms) const noexcept
{
    return std::max<std::uint64_t> (1, static_cast<std::uint64_t> (
        std::llround (ms * sampleRate_ * 0.001)));
}

double GrateEngine::swayKnotTime (std::int64_t index) const noexcept
{
    const auto minimum = static_cast<double> (std::min (
        parameters_.swaySpeedMinMs, parameters_.swaySpeedMaxMs));
    const auto maximum = static_cast<double> (std::max (
        parameters_.swaySpeedMinMs, parameters_.swaySpeedMaxMs));
    const auto period = (minimum + maximum) * 0.5;
    const auto jitter = (maximum - minimum) * 0.25;
    // Jitter each knot around a regular lattice. Adjacent distances are
    // P +/- 2J, exactly the selected transition-duration range. The large
    // SWAY-specific domains cannot alter the existing event/grain choices.
    const auto offset = static_cast<double> (random (
        static_cast<std::uint64_t> (index), 0x5357415954494d45ULL)) * 2.0 - 1.0;
    return static_cast<double> (index) * period + offset * jitter;
}

float GrateEngine::swayValue (std::int64_t absoluteSample) const noexcept
{
    const auto minimum = static_cast<double> (std::min (
        parameters_.swaySpeedMinMs, parameters_.swaySpeedMaxMs));
    const auto maximum = static_cast<double> (std::max (
        parameters_.swaySpeedMinMs, parameters_.swaySpeedMaxMs));
    const auto period = (minimum + maximum) * 0.5;
    const auto time = static_cast<double> (absoluteSample) * 1000.0 / sampleRate_;
    auto index = static_cast<std::int64_t> (std::floor (time / period));
    auto left = swayKnotTime (index);
    auto right = swayKnotTime (index + 1);
    // J < P/2, so a direct lookup needs at most one neighboring interval.
    // Signed indices extend the same trajectory through negative pre-roll.
    if (time < left)
    {
        right = left;
        left = swayKnotTime (--index);
    }
    else if (time >= right)
    {
        left = right;
        right = swayKnotTime (++index + 1);
    }
    const auto target = [this] (std::int64_t knot) noexcept
    {
        const auto key = static_cast<std::uint64_t> (knot);
        const auto depth = pick (parameters_.swayAmountMinPercent,
                                 parameters_.swayAmountMaxPercent, key,
                                 0x5357415944455054ULL) * 0.01f;
        const auto direction = random (key, 0x5357415956414c55ULL) * 2.0f - 1.0f;
        return depth * direction;
    };
    // Protect the lookup even at pathological near-int64-limit timestamps,
    // where double precision may merge otherwise distinct millisecond knots.
    const auto position = right > left
        ? std::clamp ((time - left) / (right - left), 0.0, 1.0) : 0.5;
    const auto blend = position * position * (3.0 - 2.0 * position);
    const auto from = target (index);
    const auto to = target (index + 1);
    return static_cast<float> (from + (to - from) * blend);
}

std::array<double, 3> GrateEngine::massSwayProfile (std::size_t layer) const noexcept
{
    const auto key = hash (0x4d4153534c415945ULL ^ layer);
    const auto depth = 0.05 + 0.20 * random (key, 0x4445505448ULL);
    const auto speed = random (key, 0x504552494f44ULL);
    const auto period = parameters_.synced
        ? (4.0 + 4.0 * speed) * 60000.0 / Timing::tempo (parameters_.tempoBpm)
        : 3000.0 + 3000.0 * speed;
    return { depth, period, random (key, 0x5048415345ULL) };
}

float GrateEngine::massSwayValue (std::size_t layer, std::int64_t absoluteSample) const noexcept
{
    const auto profile = massSwayProfile (layer);
    const auto phase = static_cast<double> (absoluteSample) * 1000.0 /
        (sampleRate_ * profile[1]) + profile[2];
    const auto knot = static_cast<std::int64_t> (std::floor (phase));
    const auto position = phase - static_cast<double> (knot);
    const auto blend = position * position * (3.0 - 2.0 * position);
    const auto key = hash (0x4d4153534c415945ULL ^ layer);
    const auto from = random (key, hash (static_cast<std::uint64_t> (knot)) ^ 0x56414c5545ULL) * 2.0 - 1.0;
    const auto to = random (key, hash (static_cast<std::uint64_t> (knot + 1)) ^ 0x56414c5545ULL) * 2.0 - 1.0;
    return static_cast<float> (profile[0] * (from + (to - from) * blend));
}

float GrateEngine::massLayerWeight (std::size_t layer) noexcept
{
    static constexpr std::array<float, maxStreams> weights {{
        1.0f, 0.85f, 0.7225f, 0.614125f, 0.52200625f,
        0.4437053125f, 0.377149515625f, 0.32057708828125f
    }};
    return weights[std::min (layer, maxStreams - 1)];
}

void GrateEngine::prepare (double sampleRate)
{
    sampleRate_ = std::clamp (std::isfinite (sampleRate) ? sampleRate : 48000.0,
                              8000.0, 384000.0);
    capacity_ = static_cast<std::size_t> (std::ceil (sampleRate_ * Timing::maximumCaptureMilliseconds * 0.001)) + 8;
    history_.assign (capacity_ * 2, 0.0f);
    for (auto& event : events_)
        event.samples.assign (capacity_ * 2, 0.0f);

    // A small shared lookup table makes even very short grains inexpensive.
    // The midpoint is exactly the sine window; the last curve has a plateau.
    for (std::size_t curve = 0; curve < curveCount; ++curve)
    {
        const auto shape = static_cast<double> (curve) / (curveCount - 1);
        for (std::size_t point = 0; point < curveSize; ++point)
        {
            const auto x = static_cast<double> (point) / (curveSize - 1);
            const auto sine = std::sin (x * pi * 0.5);
            const auto value = shape <= 0.5
                ? x + (sine - x) * shape * 2.0
                : (point == 0 ? 0.0 : std::pow (sine, 2.0 - shape * 2.0));
            windows_[curve][point] = static_cast<float> (value);
        }
    }
    fastAttack_ = coefficient (0.4, sampleRate_);
    fastRelease_ = coefficient (4.0, sampleRate_);
    slowAttack_ = coefficient (35.0, sampleRate_);
    slowRelease_ = coefficient (90.0, sampleRate_);
    volumeAttack_ = coefficient (0.7, sampleRate_);
    volumeRelease_ = coefficient (15.0, sampleRate_);
    gainAttack_ = coefficient (3.0, sampleRate_);
    gainRelease_ = coefficient (30.0, sampleRate_);
    massSmoothing_ = coefficient (10.0, sampleRate_);
    wetDucker_.prepare (sampleRate_);
    wetEq_.prepare (sampleRate_);
    setParameters (parameters_);
    dryTarget_ = dbGain (parameters_.dryDb);
    wetTarget_ = dbGain (parameters_.wetDb);
    threshold_ = std::pow (10.0f, parameters_.thresholdDb * 0.05f);
    reset();
}

void GrateEngine::reset (std::int64_t timelineSample) noexcept
{
    timeline_ = timelineSample;
    triggerLockUntil_ = timelineSample;
    pendingNotes_ = 0;
    historyWritten_ = 0;
    fast_ = slow_ = volume_ = activity_ = 0.0f;
    transientHigh_ = volumeHigh_ = false;
    dryGain_ = dryTarget_;
    wetGain_ = wetTarget_;
    wetDucker_.reset();
    wetEq_.reset();
    wetBudgetGain_ = 1.0f;
    bypassMix_ = parameters_.bypass ? 1.0f : 0.0f;
    currentMode_ = parameters_.mode;
    for (auto& event : events_)
    {
        event.active = false;
        event.written = 0;
        for (auto& grain : event.grains)
            grain.active = false;
    }
}

void GrateEngine::setParameters (const Parameters& values) noexcept
{
    auto p = values;
    p.mode = std::clamp (p.mode, 0, 3);
    p.amount = finiteClamp (p.amount, 0, 100, 100);
    p.sensitivity = finiteClamp (p.sensitivity, 0, 100, 50);
    p.thresholdDb = finiteClamp (p.thresholdDb, -60, 0, -24);
    p.durationMinimumMs = finiteClamp (p.durationMinimumMs, 1,
        static_cast<float> (Timing::maximumCaptureMilliseconds), 40);
    p.durationMaximumMs = finiteClamp (p.durationMaximumMs, p.durationMinimumMs,
        static_cast<float> (Timing::maximumCaptureMilliseconds), 6000);
    p.tempoBpm = static_cast<float> (Timing::tempo (p.tempoBpm));
    const auto low = p.durationMinimumMs;
    const auto high = p.durationMaximumMs;
    p.scatterMs = finiteClamp (p.scatterMs, low, high, 2000);
    p.grainMinMs = finiteClamp (p.grainMinMs, low, high, 200);
    p.grainMaxMs = finiteClamp (p.grainMaxMs, low, high, 1000);
    p.mass = std::clamp (p.mass, 1, static_cast<int> (maxStreams));
    p.panMinPercent = finiteClamp (p.panMinPercent, -100, 100, 0);
    p.panMaxPercent = finiteClamp (p.panMaxPercent, -100, 100, 0);
    p.envMinPercent = finiteClamp (p.envMinPercent, 0, 100, 0);
    p.envMaxPercent = finiteClamp (p.envMaxPercent, 0, 100, 50);
    p.undulationMinPercent = finiteClamp (p.undulationMinPercent, -100, 100, 0);
    p.undulationMaxPercent = finiteClamp (p.undulationMaxPercent, -100, 100, 50);
    p.speedMinPercent = finiteClamp (p.speedMinPercent, 1, 400, 50);
    p.speedMaxPercent = finiteClamp (p.speedMaxPercent, 1, 400, 200);
    p.attackMinMs = finiteClamp (p.attackMinMs, low, high, 500);
    p.attackMaxMs = finiteClamp (p.attackMaxMs, low, high, 1000);
    p.releaseMinMs = finiteClamp (p.releaseMinMs, low, high, 800);
    p.releaseMaxMs = finiteClamp (p.releaseMaxMs, low, high, 6000);
    p.duckWetPercent = finiteClamp (p.duckWetPercent, 0, 100, 0);
    p.duckPreviousPercent = finiteClamp (p.duckPreviousPercent, 0, 100, 50);
    p.dryDb = finiteClamp (p.dryDb, -60, 6, 0);
    p.wetDb = finiteClamp (p.wetDb, -60, 6, 0);
    p.eqLowCutHz = finiteClamp (p.eqLowCutHz, 20, 20000, 20);
    p.eqHighCutHz = finiteClamp (p.eqHighCutHz, 20, 20000, 20000);
    if (p.eqLowCutHz > p.eqHighCutHz) std::swap (p.eqLowCutHz, p.eqHighCutHz);
    wetEq_.setCutoffs (p.eqLowCutHz, p.eqHighCutHz);
    p.motionMode = std::clamp (p.motionMode, 0, 1);
    p.swayAmountMinPercent = finiteClamp (p.swayAmountMinPercent, 0, 100, 10);
    p.swayAmountMaxPercent = finiteClamp (p.swayAmountMaxPercent, 0, 100, 30);
    p.swaySpeedMinMs = finiteClamp (p.swaySpeedMinMs, low, high, 500);
    p.swaySpeedMaxMs = finiteClamp (p.swaySpeedMaxMs, low, high, 1500);
    if (p.dryDb != parameters_.dryDb)
        dryTarget_ = dbGain (p.dryDb);
    if (p.wetDb != parameters_.wetDb)
        wetTarget_ = dbGain (p.wetDb);
    if (p.thresholdDb != parameters_.thresholdDb)
        threshold_ = std::pow (10.0f, p.thresholdDb * 0.05f);
    parameters_ = p;
}

void GrateEngine::noteOn() noexcept
{
    // A malformed or hostile MIDI stream cannot grow an unbounded queue.
    pendingNotes_ = std::min<std::uint32_t> (pendingNotes_ + 1, 64);
}

void GrateEngine::retire (Event& event, double milliseconds) noexcept
{
    const auto duration = static_cast<std::uint32_t> (samplesForMs (milliseconds));
    if (!event.retiring || duration < event.pressureRemaining)
    {
        // Restart from the *current* gain, so repeated pressure never jumps.
        event.pressureTotal = duration;
        event.pressureRemaining = duration;
        event.retiring = true;
    }
}

GrateEngine::Event* GrateEngine::startEvent (bool live, bool held,
                                           std::uint64_t ordinal) noexcept
{
    Event* freeSlot = nullptr;
    Event* oldest = nullptr;
    std::size_t active = 0;
    for (auto& event : events_)
    {
        if (!event.active)
        {
            if (freeSlot == nullptr)
                freeSlot = &event;
            continue;
        }
        ++active;
        if (!event.retiring && (oldest == nullptr || event.age > oldest->age))
            oldest = &event;
    }
    // Four normal tails plus two retirement slots absorb quick retriggers.
    // Beyond this budget, fade old work and coalesce excess triggers, rather
    // than replacing a buffer that an audible grain is still reading.
    if (active >= 4 && oldest != nullptr)
        retire (*oldest, 15.0);
    if (freeSlot == nullptr)
        return nullptr;

    const auto key = hash (static_cast<std::uint64_t> (timeline_) ^ hash (ordinal));
    for (auto& previous : events_)
    {
        if (!previous.active || parameters_.mode == 0 || parameters_.duckPreviousPercent <= 0.0f)
            continue;
        const auto remaining = previous.releasing
            ? std::max<std::uint64_t> (1, previous.release -
                std::min (previous.release, previous.releaseAge))
            : previous.release;
        const auto duration = std::max (8.0, static_cast<double> (remaining) *
            (1.0 - parameters_.duckPreviousPercent * 0.01) * 1000.0 / sampleRate_);
        retire (previous, duration);
    }

    auto& event = *freeSlot;
    event.active = true;
    event.live = live;
    event.held = held;
    event.releasing = event.retiring = false;
    event.key = key;
    event.written = event.age = event.releaseAge = event.grainSerial = 0;
    event.attack = samplesForMs (pick (parameters_.attackMinMs,
                                     parameters_.attackMaxMs, key, 1));
    event.release = samplesForMs (pick (parameters_.releaseMinMs,
                                      parameters_.releaseMaxMs, key, 2));
    if (parameters_.synced)
    {
        event.attack = samplesForMs (Timing::quantizeMilliseconds (
            event.attack * 1000.0 / sampleRate_, parameters_.tempoBpm));
        event.release = samplesForMs (Timing::quantizeMilliseconds (
            event.release * 1000.0 / sampleRate_, parameters_.tempoBpm));
    }
    event.captureLimit = std::min<std::uint64_t> (capacity_, samplesForMs (
        parameters_.scatterMs));
    // Keep a little pre-trigger context, then capture a seeded 50..95 percent
    // of BUFFER after firing. Missing transport-start history is known silence.
    event.capturePostFrames = live ? event.captureLimit : std::clamp<std::uint64_t> (
        static_cast<std::uint64_t> (std::llround (event.captureLimit *
            (0.50 + 0.45 * random (key, 0x43415054555245ULL)))), 1, event.captureLimit);
    const auto prefix = event.captureLimit - event.capturePostFrames;
    event.historyCount = std::min ({ prefix, historyWritten_, static_cast<std::uint64_t> (capacity_) });
    event.historyBegin = historyWritten_ - event.historyCount;
    event.historyCopied = 0;
    event.historyZeroFrames = prefix - event.historyCount;
    event.written = prefix;
    event.previousGrain.fill (0);
    const auto first = std::min (samplesForMs (parameters_.durationMinimumMs), event.captureLimit);
    const auto stagger = std::max<std::uint64_t> (1, first / (2 * maxStreams));
    for (std::size_t stream = 0; stream < maxStreams; ++stream)
        event.nextGrain[stream] = static_cast<std::int64_t> ((live ? first : 0) + stream * stagger);
    event.layerSerial.fill (0);
    event.layerActivationSerial.fill (0);
    event.layerEnabled.fill (false);
    event.layerEnabled[0] = true;
    event.layerGain.fill (0.0f);
    event.layerGain[0] = 1.0f;
    event.audioStart = 0;
    event.audioStarted = false;
    event.releaseStart = 1.0f;
    event.envelope = 0.0f;
    event.undulation = pick (parameters_.undulationMinPercent,
                             parameters_.undulationMaxPercent, key, 3) * 0.01f;
    event.speed = pick (parameters_.speedMinPercent,
                        parameters_.speedMaxPercent, key, 4) * 0.01f;
    // Each event keeps its location within the GRAIN range. Moving either
    // endpoint still automates running events, without rerolling grain size
    // and fighting the direction of the event's undulation.
    event.grainQuantile = random (key, 5);
    event.layerQuantile[0] = event.grainQuantile;
    for (std::size_t layer = 1; layer < maxStreams; ++layer)
        event.layerQuantile[layer] = random (hash (key ^ hash (0x4d415353ULL + layer)), 5);
    event.pressureGain = 1.0f;
    event.pressureRemaining = 0;
    for (auto& grain : event.grains)
        grain.active = false;
    if (!live)
        triggerLockUntil_ = timeline_ + static_cast<std::int64_t> (event.attack);
    return &event;
}

void GrateEngine::startRelease (Event& event) noexcept
{
    if (!event.releasing)
    {
        event.releasing = true;
        event.releaseAge = 0;
        event.releaseStart = event.envelope;
    }
}

void GrateEngine::launchGrain (Event& event, std::size_t stream) noexcept
{
    const auto audible = stream < static_cast<std::size_t> (parameters_.mass) || event.layerGain[stream] > 0.0f;
    Grain* freeGrain = nullptr;
    // A fixed reservation keeps auxiliary layers from changing the main
    // layer's capacity, random choices, or launch rhythm at any MASS setting.
    if (audible)
    {
        const auto first = stream == 0 ? 0 : mainLayerGrains + (stream - 1) * extraLayerGrains;
        const auto count = stream == 0 ? mainLayerGrains : extraLayerGrains;
        for (std::size_t index = first; index < first + count; ++index)
            if (!event.grains[index].active) { freeGrain = &event.grains[index]; break; }
    }
    const auto available = std::min ({ event.written, static_cast<std::uint64_t> (capacity_),
                                      samplesForMs (parameters_.scatterMs) });
    // A grain is one contiguous, already-captured slice. Never replay a tiny
    // startup buffer inside a longer envelope: that manufactured pitched tones.
    const auto minimumCapture = parameters_.synced
        ? samplesForMs (Timing::minimumDuration (true, parameters_.tempoBpm))
        : std::min (samplesForMs (parameters_.durationMinimumMs),
            event.live ? samplesForMs (parameters_.scatterMs) : event.captureLimit);
    if (available < minimumCapture)
    {
        event.nextGrain[stream] = static_cast<std::int64_t> (event.age + samplesForMs (1));
        return;
    }
    const auto serial = event.layerSerial[stream]++;
    const auto key = stream == 0
        ? hash (event.key ^ hash (serial + 0x10000))
        : hash (event.key ^ hash (serial + 0x10000) ^ hash (0x4d415353ULL + stream));
    const auto minimum = std::min (parameters_.grainMinMs, parameters_.grainMaxMs);
    const auto maximum = std::max (parameters_.grainMinMs, parameters_.grainMaxMs);
    double size = minimum + (maximum - minimum) * event.layerQuantile[stream];
    const auto undulating = stream == 0 && parameters_.mode != 0 && parameters_.motionMode == 0;
    if (stream != 0)
    {
        // SYNC chooses an auxiliary layer's base note, while its deliberately
        // weak slow SWAY remains continuous instead of vanishing in grid snaps.
        if (parameters_.synced)
            size = Timing::quantizeMilliseconds (size, parameters_.tempoBpm, parameters_.grainTriplets);
        size *= 1.0 + massSwayValue (stream, timeline_);
    }
    else if (!undulating)
        size *= 1.0 + swayValue (timeline_);
    else
    {
        const auto progress = std::clamp (static_cast<double> (event.age) /
            (static_cast<double> (event.attack + event.release) * event.speed), 0.0, 1.0);
        const auto destination = event.undulation < 0.0f
            ? minimum : parameters_.durationMaximumMs;
        size += (destination - size) * std::abs (event.undulation) * progress;
    }
    size = std::clamp (size, static_cast<double> (parameters_.durationMinimumMs),
                            static_cast<double> (parameters_.durationMaximumMs));
    if (parameters_.synced && stream == 0)
        size = Timing::quantizeMilliseconds (size, parameters_.tempoBpm, parameters_.grainTriplets);
    auto duration = static_cast<std::uint32_t> (std::min (samplesForMs (size), available));
    auto& previous = event.previousGrain[stream];
    if (previous != 0)
        duration = std::min<std::uint32_t> (static_cast<std::uint32_t> (available),
            std::clamp (duration, std::max<std::uint32_t> (2, previous / 2), previous * 2));
    if (undulating)
    {
        // The chosen GRAIN minimum is also UNDULATION's floor. Apply it
        // after the transition slew so raising the handle cannot launch new
        // grains below that setting once enough source has been captured.
        const auto floorMs = parameters_.synced
            ? Timing::quantizeMilliseconds (minimum, parameters_.tempoBpm, parameters_.grainTriplets) : minimum;
        const auto floor = std::min (available, samplesForMs (floorMs));
        duration = std::max (duration, static_cast<std::uint32_t> (floor));
    }
    if (parameters_.synced && stream == 0)
    {
        // Compare integer frames, so rounding a beat to the sample grid cannot
        // accidentally floor an exact note duration to the preceding note.
        std::uint32_t snapped = static_cast<std::uint32_t> (minimumCapture);
        for (std::size_t step = 0; step < Timing::choiceCount (parameters_.grainTriplets); ++step)
        {
            const auto frames = samplesForMs (Timing::syncMilliseconds (
                Timing::syncNormalized (step, parameters_.grainTriplets), parameters_.tempoBpm,
                parameters_.grainTriplets));
            if (frames <= duration)
                snapped = static_cast<std::uint32_t> (frames);
        }
        duration = snapped;
    }
    previous = duration;
    event.nextGrain[stream] = static_cast<std::int64_t> (event.age +
        std::max<std::uint32_t> (1, duration / 2));
    // Inaudible layers retain their deterministic clocks and counters, but do
    // not allocate voices or read audio. Their future path is MASS independent.
    if (freeGrain == nullptr)
        return;
    ++event.grainSerial;
    auto& grain = *freeGrain;
    grain.active = true;
    grain.layer = static_cast<std::uint8_t> (stream);
    grain.serial = serial;
    grain.age = grain.position = 0;
    grain.duration = duration;
    const auto shape = pick (parameters_.envMinPercent, parameters_.envMaxPercent,
                             key, 11) * 0.01f * (curveCount - 1);
    grain.curve = static_cast<std::uint16_t> (std::min<float> (curveCount - 2, std::floor (shape)));
    grain.curveMix = shape - grain.curve;
    const auto pan = pick (parameters_.panMinPercent, parameters_.panMaxPercent,
                           key, 0x50414e) * 0.01f;
    // Stereo balance: center preserves both channels and their original level.
    grain.leftGain = pan <= 0.0f ? 1.0f : std::cos (pan * static_cast<float> (pi * 0.5));
    grain.rightGain = pan >= 0.0f ? 1.0f : std::cos (-pan * static_cast<float> (pi * 0.5));
    const auto offsets = available - duration;
    const auto offset = std::min (offsets, static_cast<std::uint64_t> (
        random (key, 12) * static_cast<double> (offsets + 1)));
    grain.begin = event.written - available + offset;
    // Writer and grain advance by one frame together. The contiguous slice
    // remains valid even if its ring-buffer slots are overwritten after reading.
    if (!event.audioStarted)
    {
        event.audioStarted = true;
        event.audioStart = event.age;
    }
}

float GrateEngine::grainWindow (const Grain& grain) const noexcept
{
    const auto distance = std::min (grain.age, grain.duration - grain.age);
    const auto x = std::clamp (2.0f * static_cast<float> (distance) /
                               static_cast<float> (grain.duration), 0.0f, 1.0f);
    const auto table = x * (curveSize - 1);
    const auto index = std::min<std::size_t> (curveSize - 2, static_cast<std::size_t> (table));
    const auto fraction = table - static_cast<float> (index);
    const auto a = windows_[grain.curve][index] + fraction *
        (windows_[grain.curve][index + 1] - windows_[grain.curve][index]);
    const auto b = windows_[grain.curve + 1][index] + fraction *
        (windows_[grain.curve + 1][index + 1] - windows_[grain.curve + 1][index]);
    const auto envelope = a + (b - a) * grain.curveMix;
    // Even the square setting has a one-millisecond rounded edge.
    const auto edge = static_cast<float> (sampleRate_ * 0.001);
    return std::min (envelope, smoothstep (static_cast<float> (distance) / edge));
}

float GrateEngine::readSample (const Event& event, std::uint64_t frame,
                              unsigned channel) const noexcept
{
    if (frame < event.historyZeroFrames)
        return 0.0f;
    if (frame < event.historyZeroFrames + event.historyCount)
    {
        const auto prefixFrame = frame - event.historyZeroFrames;
        if (prefixFrame >= event.historyCopied)
            return history_[static_cast<std::size_t> ((event.historyBegin + prefixFrame) % capacity_) * 2 + channel];
    }
    return event.samples[static_cast<std::size_t> (frame % capacity_) * 2 + channel];
}

std::array<float, 2> GrateEngine::renderEvent (Event& event) noexcept
{
    if (event.age < event.attack)
        event.envelope = static_cast<float> (event.age) / static_cast<float> (event.attack);
    else if (!event.releasing)
    {
        event.envelope = 1.0f;
        if (!event.live && !event.held)
            startRelease (event);
    }
    if (event.releasing)
    {
        const auto progress = std::min (1.0f, static_cast<float> (event.releaseAge) /
                                                static_cast<float> (event.release));
        const auto remaining = 1.0f - progress;
        event.envelope = event.releaseStart * remaining * remaining;
        if (event.releaseAge++ >= event.release)
        {
            event.active = false;
            return {};
        }
    }

    if (event.retiring)
    {
        if (event.pressureRemaining == 0)
        {
            event.active = false;
            return {};
        }
        // Multiplicative recurrence preserves continuity if another trigger
        // shortens an already retiring tail.
        const auto remaining = event.pressureRemaining--;
        event.pressureGain *= static_cast<float> (remaining - 1) / remaining;
    }
    for (std::size_t layer = 1; layer < maxStreams; ++layer)
    {
        const auto enabled = layer < static_cast<std::size_t> (parameters_.mass);
        if (enabled && !event.layerEnabled[layer] && event.layerGain[layer] == 0.0f)
            event.layerActivationSerial[layer] = event.layerSerial[layer];
        event.layerEnabled[layer] = enabled;
    }
    for (std::size_t stream = 0; stream < maxStreams; ++stream)
        if (static_cast<std::int64_t> (event.age) >= event.nextGrain[stream])
            launchGrain (event, stream);

    std::array<std::array<float, 2>, maxStreams> layerOutput {};
    std::array<float, maxStreams> layerWindowSum {};
    std::array<bool, maxStreams> layerRampReady {};
    for (auto& grain : event.grains)
    {
        if (!grain.active)
            continue;
        const auto window = grainWindow (grain);
        layerWindowSum[grain.layer] += window;
        if (window > 0.0f && grain.serial >= event.layerActivationSerial[grain.layer])
            layerRampReady[grain.layer] = true;
        layerOutput[grain.layer][0] += readSample (event, grain.begin + grain.position, 0) *
                                      window * grain.leftGain;
        layerOutput[grain.layer][1] += readSample (event, grain.begin + grain.position, 1) *
                                      window * grain.rightGain;
        ++grain.position;
        if (++grain.age >= grain.duration)
            grain.active = false;
    }
    // Normalize overlaps within each layer before applying its .85^index
    // additive contribution. WET controls the resulting combined level.
    // A new layer's ramp starts only once audible samples exist;
    // otherwise normalization would cancel its first grain's envelope fade.
    std::array<float, 2> output {};
    bool hasExtraLayer = false;
    for (std::size_t layer = 1; layer < maxStreams; ++layer)
    {
        const auto target = event.layerEnabled[layer] && layerRampReady[layer] ? 1.0f : 0.0f;
        auto& gain = event.layerGain[layer];
        gain += (target - gain) * massSmoothing_;
        if (gain < 1.0e-6f) gain = 0.0f;
        else if (1.0f - gain < 1.0e-6f) gain = 1.0f;
        if (gain == 0.0f || layerWindowSum[layer] == 0.0f)
            continue;
        hasExtraLayer = true;
        const auto contribution = gain * massLayerWeight (layer);
        const auto scale = contribution / std::max (1.0e-12f, layerWindowSum[layer]);
        output[0] += layerOutput[layer][0] * scale;
        output[1] += layerOutput[layer][1] * scale;
    }
    const auto startup = samplesForMs (5);
    const auto elapsed = event.audioStarted ? event.age - event.audioStart : 0;
    const auto startupGain = smoothstep (static_cast<float> (elapsed) /
                                         static_cast<float> (startup));
    const auto commonGain = event.envelope * event.pressureGain * startupGain;
    if (!hasExtraLayer)
    {
        // Preserve the existing MASS1 arithmetic and resulting sample values.
        const auto gain = commonGain / std::max (1.0e-12f, layerWindowSum[0]);
        output[0] = layerOutput[0][0] * gain;
        output[1] = layerOutput[0][1] * gain;
    }
    else
    {
        const auto scale = 1.0f / std::max (1.0e-12f, layerWindowSum[0]);
        output[0] += layerOutput[0][0] * scale;
        output[1] += layerOutput[0][1] * scale;
        output[0] *= commonGain;
        output[1] *= commonGain;
    }
    ++event.age;
    return output;
}

float GrateEngine::protect (float sample) noexcept
{
    const auto magnitude = std::abs (sample);
    if (magnitude <= 0.98f)
        return sample;
    // Unity below -0.18 dBFS; a continuous-slope safety knee above it. This
    // affects exceptional peaks only and guarantees finite, bounded output.
    return std::copysign (0.98f + 0.02f * std::tanh ((magnitude - 0.98f) * 50.0f), sample);
}

std::array<float, 2> GrateEngine::process (float left, float right) noexcept
{
    left = finiteClamp (left, -64, 64, 0);
    right = finiteClamp (right, -64, 64, 0);
    if (capacity_ == 0)
        return { left, right };
    const auto level = std::max (std::abs (left), std::abs (right));
    fast_ += (level - fast_) * (level > fast_ ? fastAttack_ : fastRelease_);
    slow_ += (level - slow_) * (level > slow_ ? slowAttack_ : slowRelease_);
    volume_ += (level - volume_) * (level > volume_ ? volumeAttack_ : volumeRelease_);

    if (parameters_.mode != currentMode_)
    {
        for (auto& event : events_)
            if (event.active)
            {
                event.live = false;
                event.held = false;
                retire (event, 15.0);
            }
        currentMode_ = parameters_.mode;
        triggerLockUntil_ = timeline_;
        transientHigh_ = volumeHigh_ = false;
    }

    const auto sensitivity = parameters_.sensitivity * 0.01f;
    const auto onset = fast_ > slow_ * (2.5f - sensitivity * 1.4f) &&
                       fast_ > (0.035f - sensitivity * 0.034f);
    if (fast_ < slow_ * 1.04f)
        transientHigh_ = false;
    const auto newOnset = onset && !transientHigh_;
    if (onset)
        transientHigh_ = true;

    if (currentMode_ == 0)
    {
        bool hasLive = false;
        for (const auto& event : events_)
            hasLive = hasLive || (event.active && event.live && !event.retiring);
        if (!hasLive)
            startEvent (true, true, 0);
    }
    else if (currentMode_ == 1 && newOnset && timeline_ >= triggerLockUntil_)
        startEvent (false, false, 0);
    else if (currentMode_ == 2)
    {
        for (std::uint32_t note = 0; note < pendingNotes_; ++note)
            if (parameters_.amount >= 100.0f || (parameters_.amount > 0.0f &&
                random (static_cast<std::uint64_t> (timeline_), 0x90000 + note) <
                    parameters_.amount * 0.01f))
                startEvent (false, false, note);
    }
    else if (currentMode_ == 3)
    {
        if (volumeHigh_)
            volumeHigh_ = volume_ >= threshold_ * 0.85f;
        else
            volumeHigh_ = volume_ >= threshold_;
        Event* held = nullptr;
        for (auto& event : events_)
            if (event.active && event.held && !event.live && !event.retiring)
                held = &event;
        if (held != nullptr && !volumeHigh_ && held->age >= held->attack)
        {
            held->held = false;
            startRelease (*held);
        }
        if (volumeHigh_ && held == nullptr && timeline_ >= triggerLockUntil_)
            startEvent (false, true, 0);
    }
    pendingNotes_ = 0;

    std::array<float, 2> wet {};
    float envelopeSum = 0.0f;
    for (auto& event : events_)
    {
        if (!event.active)
            continue;
        // Stage only one old prefix frame per event per callback. Any prefix
        // not staged yet remains readable directly from the shared history.
        // Prefix <= post quota, so all real history is owned locally by freeze.
        if (event.historyCopied < event.historyCount)
        {
            const auto source = static_cast<std::size_t> ((event.historyBegin + event.historyCopied) % capacity_) * 2;
            const auto destination = static_cast<std::size_t> (event.historyZeroFrames + event.historyCopied) * 2;
            event.samples[destination] = history_[source];
            event.samples[destination + 1] = history_[source + 1];
            ++event.historyCopied;
        }
        if (event.live || event.written < event.captureLimit)
        {
            const auto position = static_cast<std::size_t> (event.written % capacity_) * 2;
            event.samples[position] = left;
            event.samples[position + 1] = right;
            ++event.written;
        }
        const auto audio = renderEvent (event);
        wet[0] += audio[0];
        wet[1] += audio[1];
        const auto envelope = event.active ? event.envelope * event.pressureGain : 0.0f;
        envelopeSum += envelope;
    }
    // Record after every event has read/copied history: the triggering sample
    // belongs to the post-trigger body, never to its pre-trigger context.
    const auto historyPosition = static_cast<std::size_t> (historyWritten_ % capacity_) * 2;
    history_[historyPosition] = left;
    history_[historyPosition + 1] = right;
    ++historyWritten_;
    // Prevent a pile of long tails from multiplying perceived level. The
    // slow gain recovery keeps retiring tails from pumping their successors.
    const auto budgetTarget = 1.0f / std::max (1.0f, envelopeSum);
    wetBudgetGain_ += (budgetTarget - wetBudgetGain_) *
        (budgetTarget < wetBudgetGain_ ? gainAttack_ : gainRelease_);
    wet = wetEq_.process (wet);
    const auto wetDuckGain = wetDucker_.process (left, right, parameters_.duckWetPercent);
    dryGain_ += (dryTarget_ - dryGain_) * gainAttack_;
    wetGain_ += (wetTarget_ - wetGain_) * gainAttack_;
    const auto bypassTarget = parameters_.bypass ? 1.0f : 0.0f;
    const auto previousBypassMix = bypassMix_;
    bypassMix_ += (bypassTarget - bypassMix_) * gainAttack_;
    // Float smoothing may stop advancing several ULPs from the target,
    // especially at high sample rates. Snap when it stalls so full bypass
    // becomes mathematically exact rather than retaining a tiny wet leak.
    if (std::abs (bypassTarget - bypassMix_) < 0.000001f ||
        bypassMix_ == previousBypassMix)
        bypassMix_ = bypassTarget;
    const auto wetActivity = std::max (std::abs (wet[0]), std::abs (wet[1])) *
        wetGain_ * wetBudgetGain_ * wetDuckGain;
    activity_ += (wetActivity - activity_) * (wetActivity > activity_ ? gainAttack_ : gainRelease_);
    std::array<float, 2> result;
    const std::array<float, 2> dry {{ left, right }};
    for (unsigned channel = 0; channel != 2; ++channel)
    {
        const auto processed = dry[channel] * dryGain_ +
            wet[channel] * wetGain_ * wetBudgetGain_ * wetDuckGain;
        // Peak protection belongs to the effected branch. Full bypass must
        // preserve the original input, including legitimate interstage peaks.
        result[channel] = protect (processed) * (1.0f - bypassMix_) +
                          dry[channel] * bypassMix_;
    }
    if (timeline_ < std::numeric_limits<std::int64_t>::max())
        ++timeline_;
    return result;
}

std::size_t GrateEngine::memoryBytes() const noexcept
{
    std::size_t bytes = sizeof (*this) + history_.capacity() * sizeof (float);
    for (const auto& event : events_)
        bytes += event.samples.capacity() * sizeof (float);
    return bytes;
}

std::size_t GrateEngine::activeEvents() const noexcept
{
    std::size_t count = 0;
    for (const auto& event : events_)
        count += event.active ? 1 : 0;
    return count;
}
}
