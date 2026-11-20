#include "GrainEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace InhaGrain
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

std::uint64_t GrainEngine::hash (std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

float GrainEngine::random (std::uint64_t key, std::uint64_t domain) const noexcept
{
    const auto value = hash (key ^ hash (domain) ^
                             (static_cast<std::uint64_t> (parameters_.seed) << 17));
    return static_cast<float> (value >> 40) * (1.0f / 16777216.0f);
}

float GrainEngine::pick (float minimum, float maximum, std::uint64_t key,
                        std::uint64_t domain) const noexcept
{
    if (minimum > maximum)
        std::swap (minimum, maximum);
    return minimum + (maximum - minimum) * random (key, domain);
}

std::uint64_t GrainEngine::samplesForMs (double ms) const noexcept
{
    return std::max<std::uint64_t> (1, static_cast<std::uint64_t> (
        std::llround (ms * sampleRate_ * 0.001)));
}

void GrainEngine::prepare (double sampleRate)
{
    sampleRate_ = std::clamp (std::isfinite (sampleRate) ? sampleRate : 48000.0,
                              8000.0, 384000.0);
    capacity_ = static_cast<std::size_t> (std::ceil (sampleRate_ * 20.0)) + 8;
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
    setParameters (parameters_);
    reset();
}

void GrainEngine::reset (std::int64_t timelineSample) noexcept
{
    timeline_ = timelineSample;
    triggerLockUntil_ = timelineSample;
    pendingNotes_ = 0;
    fast_ = slow_ = volume_ = activity_ = 0.0f;
    transientHigh_ = volumeHigh_ = false;
    dryGain_ = dryTarget_;
    wetGain_ = wetTarget_;
    dryDuck_ = wetBudgetGain_ = 1.0f;
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

void GrainEngine::setParameters (const Parameters& values) noexcept
{
    auto p = values;
    p.mode = std::clamp (p.mode, 0, 3);
    p.amount = finiteClamp (p.amount, 0, 100, 100);
    p.sensitivity = finiteClamp (p.sensitivity, 0, 100, 50);
    p.thresholdDb = finiteClamp (p.thresholdDb, -60, 0, -24);
    p.scatterMs = finiteClamp (p.scatterMs, 10, 20000, 2000);
    p.grainMinMs = finiteClamp (p.grainMinMs, 5, 10000, 200);
    p.grainMaxMs = finiteClamp (p.grainMaxMs, 5, 10000, 1000);
    p.envMinPercent = finiteClamp (p.envMinPercent, 0, 100, 0);
    p.envMaxPercent = finiteClamp (p.envMaxPercent, 0, 100, 50);
    p.undulationMinPercent = finiteClamp (p.undulationMinPercent, -100, 100, 0);
    p.undulationMaxPercent = finiteClamp (p.undulationMaxPercent, -100, 100, 50);
    p.speedMinPercent = finiteClamp (p.speedMinPercent, 1, 400, 50);
    p.speedMaxPercent = finiteClamp (p.speedMaxPercent, 1, 400, 200);
    p.attackMinMs = finiteClamp (p.attackMinMs, 1, 10000, 500);
    p.attackMaxMs = finiteClamp (p.attackMaxMs, 1, 10000, 1000);
    p.releaseMinMs = finiteClamp (p.releaseMinMs, 5, 30000, 800);
    p.releaseMaxMs = finiteClamp (p.releaseMaxMs, 5, 30000, 10000);
    p.duckDryPercent = finiteClamp (p.duckDryPercent, 0, 100, 90);
    p.duckPreviousPercent = finiteClamp (p.duckPreviousPercent, 0, 100, 90);
    p.dryDb = finiteClamp (p.dryDb, -60, 6, 0);
    p.wetDb = finiteClamp (p.wetDb, -60, 6, 0);
    if (p.dryDb != parameters_.dryDb)
        dryTarget_ = dbGain (p.dryDb);
    if (p.wetDb != parameters_.wetDb)
        wetTarget_ = dbGain (p.wetDb);
    if (p.thresholdDb != parameters_.thresholdDb)
        threshold_ = std::pow (10.0f, p.thresholdDb * 0.05f);
    parameters_ = p;
}

void GrainEngine::noteOn() noexcept
{
    // A malformed or hostile MIDI stream cannot grow an unbounded queue.
    pendingNotes_ = std::min<std::uint32_t> (pendingNotes_ + 1, 64);
}

void GrainEngine::retire (Event& event, double milliseconds) noexcept
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

GrainEngine::Event* GrainEngine::startEvent (bool live, bool held,
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
        if (!previous.active || parameters_.duckPreviousPercent <= 0.0f)
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
    event.captureLimit = std::min<std::uint64_t> (capacity_, samplesForMs (
        parameters_.scatterMs));
    event.previousGrain = 0;
    event.nextGrain = static_cast<std::int64_t> (samplesForMs (5));
    event.releaseStart = 1.0f;
    event.envelope = 0.0f;
    event.undulation = pick (parameters_.undulationMinPercent,
                             parameters_.undulationMaxPercent, key, 3) * 0.01f;
    event.speed = pick (parameters_.speedMinPercent,
                        parameters_.speedMaxPercent, key, 4) * 0.01f;
    event.pressureGain = 1.0f;
    event.pressureRemaining = 0;
    for (auto& grain : event.grains)
        grain.active = false;
    if (!live)
        triggerLockUntil_ = timeline_ + static_cast<std::int64_t> (event.attack);
    return &event;
}

void GrainEngine::startRelease (Event& event) noexcept
{
    if (!event.releasing)
    {
        event.releasing = true;
        event.releaseAge = 0;
        event.releaseStart = event.envelope;
    }
}

void GrainEngine::launchGrain (Event& event) noexcept
{
    Grain* freeGrain = nullptr;
    for (auto& grain : event.grains)
        if (!grain.active) { freeGrain = &grain; break; }
    if (freeGrain == nullptr || event.written < 8)
    {
        // Normally impossible with the duration slew bound; still bounded
        // if a future parameter extension introduces an extreme ratio.
        event.nextGrain = static_cast<std::int64_t> (event.age + samplesForMs (2.5));
        return;
    }
    const auto serial = event.grainSerial++;
    const auto key = hash (event.key ^ hash (serial + 0x10000));
    double size = pick (parameters_.grainMinMs, parameters_.grainMaxMs, key, 10);
    const auto progress = std::clamp (static_cast<double> (event.age) /
        (static_cast<double> (event.attack + event.release) * event.speed), 0.0, 1.0);
    const auto destination = event.undulation < 0.0f ? 5.0 : 10000.0;
    size += (destination - size) * std::abs (event.undulation) * progress;
    auto duration = static_cast<std::uint32_t> (samplesForMs (size));
    if (event.previousGrain != 0)
        duration = std::clamp (duration, std::max<std::uint32_t> (2, event.previousGrain / 2),
                               event.previousGrain * 2);
    event.previousGrain = duration;

    auto& grain = *freeGrain;
    grain.active = true;
    grain.age = 0;
    grain.duration = duration;
    const auto shape = pick (parameters_.envMinPercent, parameters_.envMaxPercent,
                             key, 11) * 0.01f * (curveCount - 1);
    grain.curve = static_cast<std::uint16_t> (std::min<float> (curveCount - 2, std::floor (shape)));
    grain.curveMix = shape - grain.curve;

    auto available = std::min<std::uint64_t> (event.written, capacity_);
    auto begin = event.written - available;
    if (event.live)
    {
        const auto requested = samplesForMs (parameters_.scatterMs);
        available = std::min (available, requested);
        begin = event.written - available;
        // Reserve enough distance from the live writer that no sample read
        // by this grain can be overwritten before its envelope finishes.
        const auto oldestSafe = event.written > capacity_ - duration
            ? event.written - (capacity_ - duration) : 0;
        if (begin < oldestSafe)
        {
            available -= std::min (available - 1, oldestSafe - begin);
            begin = oldestSafe;
        }
    }
    grain.begin = begin;
    grain.crossfade = static_cast<std::uint32_t> (std::min<std::uint64_t> (
        samplesForMs (2), available / 4));
    grain.period = static_cast<std::uint32_t> (std::max<std::uint64_t> (
        1, available - grain.crossfade));
    grain.position = std::min (grain.period - 1, static_cast<std::uint32_t> (
        random (key, 12) * grain.period));

    // Every grain schedules the following grain at its halfway point.
    event.nextGrain = static_cast<std::int64_t> (event.age + std::max<std::uint32_t> (1, duration / 2));
}

float GrainEngine::grainWindow (const Grain& grain) const noexcept
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

float GrainEngine::readSample (const Event& event, std::uint64_t frame,
                              unsigned channel) const noexcept
{
    return event.samples[static_cast<std::size_t> (frame % capacity_) * 2 + channel];
}

std::array<float, 2> GrainEngine::renderEvent (Event& event) noexcept
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
    if (static_cast<std::int64_t> (event.age) >= event.nextGrain)
        launchGrain (event);

    std::array<float, 2> output {};
    float weight = 0.0f;
    for (auto& grain : event.grains)
    {
        if (!grain.active)
            continue;
        const auto window = grainWindow (grain);
        weight += window;
        auto blend = 1.0f;
        if (grain.crossfade != 0 && grain.position < grain.crossfade)
            blend = smoothstep (static_cast<float> (grain.position) / grain.crossfade);
        for (unsigned channel = 0; channel != 2; ++channel)
        {
            auto sample = readSample (event, grain.begin + grain.position, channel);
            if (blend < 1.0f)
                sample = readSample (event, grain.begin + grain.period + grain.position, channel) *
                             (1.0f - blend) + sample * blend;
            output[channel] += sample * window;
        }
        if (++grain.position >= grain.period)
            grain.position = 0;
        if (++grain.age >= grain.duration)
            grain.active = false;
    }
    // Normalize the overlapping windows, never an isolated fade-in. This is
    // a partition of unity at steady state, including sine/square curves and
    // changing grain lengths; a DC source therefore has no window tremolo.
    const auto gain = event.envelope * event.pressureGain / std::max (1.0f, weight);
    output[0] *= gain;
    output[1] *= gain;
    ++event.age;
    return output;
}

float GrainEngine::protect (float sample) noexcept
{
    const auto magnitude = std::abs (sample);
    if (magnitude <= 0.98f)
        return sample;
    // Unity below -0.18 dBFS; a continuous-slope safety knee above it. This
    // affects exceptional peaks only and guarantees finite, bounded output.
    return std::copysign (0.98f + 0.02f * std::tanh ((magnitude - 0.98f) * 50.0f), sample);
}

std::array<float, 2> GrainEngine::process (float left, float right) noexcept
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
    float maximumEnvelope = 0.0f, envelopeSum = 0.0f;
    for (auto& event : events_)
    {
        if (!event.active)
            continue;
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
        maximumEnvelope = std::max (maximumEnvelope, envelope);
        envelopeSum += envelope;
    }
    // Prevent a pile of long tails from multiplying perceived level. The
    // slow gain recovery keeps retiring tails from pumping their successors.
    const auto budgetTarget = 1.0f / std::max (1.0f, envelopeSum);
    wetBudgetGain_ += (budgetTarget - wetBudgetGain_) *
        (budgetTarget < wetBudgetGain_ ? gainAttack_ : gainRelease_);
    const auto duckTarget = 1.0f - maximumEnvelope * parameters_.duckDryPercent * 0.01f;
    dryDuck_ += (duckTarget - dryDuck_) *
        (duckTarget < dryDuck_ ? gainAttack_ : gainRelease_);
    dryGain_ += (dryTarget_ - dryGain_) * gainAttack_;
    wetGain_ += (wetTarget_ - wetGain_) * gainAttack_;
    const auto bypassTarget = parameters_.bypass ? 1.0f : 0.0f;
    bypassMix_ += (bypassTarget - bypassMix_) * gainAttack_;
    const auto wetActivity = std::max (std::abs (wet[0]), std::abs (wet[1])) * wetGain_;
    activity_ += (wetActivity - activity_) * (wetActivity > activity_ ? gainAttack_ : gainRelease_);
    std::array<float, 2> result;
    const std::array<float, 2> dry {{ left, right }};
    for (unsigned channel = 0; channel != 2; ++channel)
    {
        const auto processed = dry[channel] * dryGain_ * dryDuck_ +
            wet[channel] * wetGain_ * wetBudgetGain_;
        result[channel] = protect (processed * (1.0f - bypassMix_) + dry[channel] * bypassMix_);
    }
    if (timeline_ < std::numeric_limits<std::int64_t>::max())
        ++timeline_;
    return result;
}

std::size_t GrainEngine::memoryBytes() const noexcept
{
    std::size_t bytes = sizeof (*this);
    for (const auto& event : events_)
        bytes += event.samples.capacity() * sizeof (float);
    return bytes;
}

std::size_t GrainEngine::activeEvents() const noexcept
{
    std::size_t count = 0;
    for (const auto& event : events_)
        count += event.active ? 1 : 0;
    return count;
}
}
