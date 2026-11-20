#include "GrainEngine.h"
#include "GrainTiming.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

// Track only the real-time calls under test. Preparation and test fixtures may allocate.
namespace {
thread_local bool trackAllocation = false;
std::atomic<std::uint64_t> callbackAllocations { 0 };
}
void* operator new(std::size_t n) {
    if (trackAllocation) ++callbackAllocations;
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace InhaGrain {
// Read-only inspection of scheduling values avoids guessing grain sizes from
// the spectrum of a random audio texture. The engine grants this test fixture
// friendship without adding a product API or work to the audio callback.
struct GrainEngineTestAccess {
    struct EventState {
        bool active = false;
        std::uint64_t age = 0, attack = 0, release = 0, grainSerial = 0, written = 0;
        std::uint32_t previousGrain = 0;
        float grainQuantile = 0.0f, undulation = 0.0f, speed = 0.0f;
    };
    static EventState firstEvent(const GrainEngine& engine) {
        for (const auto& event : engine.events_) {
            if (!event.active) continue;
            return { true, event.age, event.attack, event.release, event.grainSerial,
                     event.written, event.previousGrain[0], event.grainQuantile, event.undulation, event.speed };
        }
        return {};
    }
    static bool validSlices(const GrainEngine& engine) {
        for (const auto& event : engine.events_) {
            if (!event.active) continue;
            for (const auto& grain : event.grains) {
                if (!grain.active) continue;
                const auto oldest = event.written > engine.capacity_ ? event.written - engine.capacity_ : 0;
                if (grain.duration == 0 || grain.position >= grain.duration ||
                    grain.begin + grain.position < oldest ||
                    grain.begin + grain.duration > event.written)
                    return false;
            }
        }
        return true;
    }
    static std::size_t activeGrains(const GrainEngine& engine) {
        std::size_t count = 0;
        for (const auto& event : engine.events_)
            if (event.active)
                for (const auto& grain : event.grains) count += grain.active ? 1 : 0;
        return count;
    }
    static Parameters parameters(const GrainEngine& engine) { return engine.parameters_; }
    static float swayAt(const GrainEngine& engine, std::int64_t sample) {
        return engine.swayValue(sample);
    }
    static double swayKnotAt(const GrainEngine& engine, std::int64_t index) {
        return engine.swayKnotTime(index);
    }
    static float currentSway(const GrainEngine& engine) {
        return engine.swayValue(engine.timeline_);
    }
};
}

namespace {
using InhaGrain::GrainEngine;
using InhaGrain::Parameters;
constexpr double rate = 24000.0;
constexpr double pi = 3.14159265358979323846;
int failures = 0;
#define CHECK(condition, description) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, description); ++failures; } } while (false)

Parameters base() {
    Parameters p;
    p.mode = 2;
    p.amount = 100.0;
    p.sensitivity = 70.0;
    p.thresholdDb = -24.0;
    p.scatterMs = 100.0;
    p.grainMinMs = 15.0;
    p.grainMaxMs = 65.0;
    p.envMinPercent = 0.0;
    p.envMaxPercent = 50.0;
    p.undulationMinPercent = 0.0;
    p.undulationMaxPercent = 0.0;
    p.speedMinPercent = 50.0;
    p.speedMaxPercent = 200.0;
    p.attackMinMs = 15.0;
    p.attackMaxMs = 45.0;
    p.releaseMinMs = 300.0;
    p.releaseMaxMs = 550.0;
    p.duckDryPercent = 0.0;
    p.duckPreviousPercent = 0.0;
    p.dryDb = -60.0;
    p.wetDb = 0.0;
    p.seed = 0x5A1F246Bu;
    p.bypass = false;
    return p;
}

float source(int sample) {
    const double t = sample / rate;
    return static_cast<float>(0.18 * std::sin(2.0 * pi * 137.0 * t)
          + 0.09 * std::sin(2.0 * pi * 593.0 * t)
          + 0.035 * std::sin(2.0 * pi * 2347.0 * t));
}

struct Stereo {
    float left = 0.0f;
    float right = 0.0f;
};

std::vector<Stereo> deterministicRender(GrainEngine& engine, Parameters p,
                                      int blockSize, std::int64_t timeline = 0) {
    engine.setParameters(p);
    engine.reset(timeline);
    const int total = static_cast<int>(rate * 1.8);
    std::vector<Stereo> out(static_cast<std::size_t>(total));
    for (int start = 0; start < total; start += blockSize) {
        const int end = std::min(total, start + blockSize);
        for (int n = start; n < end; ++n) {
            if (n % 4567 == 0) engine.noteOn();
            if (n % 997 == 0) {
                p.grainMinMs = 5.0 + (n % 170) * 0.2;
                p.grainMaxMs = p.grainMinMs + 75.0;
                p.envMaxPercent = (n / 997) % 2 ? 100.0 : 50.0;
                engine.setParameters(p);
            }
            const auto sample = engine.process(source(n), source(n + 71));
            out[static_cast<std::size_t>(n)] = { sample[0], sample[1] };
        }
    }
    return out;
}

double meanSquare(const std::vector<Stereo>& audio, int first, int last) {
    double energy = 0.0;
    for (int i = first; i < last; ++i) {
        const auto& s = audio[static_cast<std::size_t>(i)];
        energy += s.left * s.left + s.right * s.right;
    }
    return energy / std::max(1, 2 * (last - first));
}

void testDeterminism() {
    GrainEngine a, b;
    a.prepare(rate); b.prepare(rate);
    auto p = base();
    p.undulationMinPercent = -50.0;
    p.undulationMaxPercent = 60.0;
    const auto reference = deterministicRender(a, p, 64, 987654);
    const auto differentBlocks = deterministicRender(b, p, 511, 987654);
    CHECK(reference.size() == differentBlocks.size(), "render sizes agree");
    CHECK(std::memcmp(reference.data(), differentBlocks.data(), reference.size() * sizeof(Stereo)) == 0,
          "seed and timeline render bit identically with different block partitions");
    const auto restarted = deterministicRender(a, p, 127, 987654);
    CHECK(std::memcmp(reference.data(), restarted.data(), reference.size() * sizeof(Stereo)) == 0,
          "reset replays the same timeline exactly, without stale buffer leakage");
    ++p.seed;
    const auto changedSeed = deterministicRender(b, p, 64, 987654);
    double difference = 0.0;
    for (std::size_t i = 0; i < reference.size(); ++i)
        difference += std::abs(reference[i].left - changedSeed[i].left);
    CHECK(difference > 1.0, "refreshing seed changes audible randomized grains");
    CHECK(meanSquare(reference, 0, static_cast<int>(reference.size())) > 1.0e-6,
          "deterministic test exercises audible wet audio");
}

void testMidiProbabilityAndTail() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.amount = 0.0;
    e.setParameters(p); e.reset();
    double energy = 0.0;
    for (int n = 0; n < static_cast<int>(rate); ++n) {
        if (n % 200 == 0) e.noteOn();
        const auto out = e.process(source(n), source(n));
        energy += out[0] * out[0];
    }
    CHECK(energy < 1.0e-9, "MIDI AMOUNT 0 suppresses every note and wet event");
    CHECK(e.activeEvents() == 0, "rejected MIDI notes create no events");

    p.amount = 100.0;
    e.setParameters(p); e.reset(); e.noteOn();
    double early = 0.0, late = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 1.5); ++n) {
        const auto out = e.process(source(n), source(n + 19));
        if (n < rate * 0.5) early += out[0] * out[0];
        if (n > rate * 1.3) late += out[0] * out[0];
    }
    CHECK(early > 1.0e-4, "MIDI AMOUNT 100 starts an audible granular event");
    CHECK(late < 1.0e-9, "event release ends without a stuck granular tail");
    CHECK(e.activeEvents() == 0, "completed MIDI event releases its slot");
}

void testFrozenCaptureAndLiveCapture() {
    GrainEngine frozen, live;
    frozen.prepare(rate); live.prepare(rate);
    auto p = base();
    p.scatterMs = 50.0;
    p.attackMinMs = p.attackMaxMs = 10.0;
    p.releaseMinMs = p.releaseMaxMs = 1800.0;
    p.grainMinMs = p.grainMaxMs = 20.0;
    frozen.setParameters(p); frozen.reset(); frozen.noteOn();
    p.mode = 0;
    live.setParameters(p); live.reset();
    double frozenTail = 0.0, liveTail = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 1.1); ++n) {
        // A captured constant is deliberately removed from the live input later.
        const float input = n < rate * 0.2 ? 0.15f : 0.0f;
        const auto f = frozen.process(input, input);
        const auto l = live.process(input, input);
        if (n > rate * 0.7) {
            frozenTail += f[0] * f[0];
            liveTail += l[0] * l[0];
        }
    }
    CHECK(frozenTail > 1.0e-3, "triggered SCATTER freezes source material for the event tail");
    CHECK(liveTail < 1.0e-8, "NONE rolling capture replaces old material with current silence");
    double recovery = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 0.35); ++n) {
        const auto out = live.process(source(n), source(n));
        recovery += out[0] * out[0];
    }
    CHECK(recovery > 1.0e-3, "NONE resumes immediately when fresh input returns");
}

void testVolumeHoldAndRelease() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.mode = 3;
    p.thresholdDb = -24.0;
    p.attackMinMs = p.attackMaxMs = 50.0;
    p.releaseMinMs = p.releaseMaxMs = 150.0;
    e.setParameters(p); e.reset();
    double heldEnergy = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 0.65); ++n) {
        const auto out = e.process(0.2f, 0.2f);
        if (n > rate * 0.4) heldEnergy += out[0] * out[0];
    }
    CHECK(heldEnergy > 1.0e-3, "VOLUME sustains beyond attack plus release while input exceeds threshold");
    CHECK(e.activeEvents() > 0, "VOLUME holds an event while above threshold");
    for (int n = 0; n < static_cast<int>(rate * 0.5); ++n) e.process(0.0f, 0.0f);
    CHECK(e.activeEvents() == 0, "VOLUME releases and frees event after falling below threshold");
    double retrigger = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 0.25); ++n) {
        const auto out = e.process(0.2f, 0.2f);
        retrigger += out[0] * out[0];
    }
    CHECK(retrigger > 1.0e-3, "VOLUME rearms after release and triggers on a later rise");
}

void testTransientSilenceAndLockout() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.mode = 1;
    p.sensitivity = 100.0;
    p.attackMinMs = p.attackMaxMs = 250.0;
    p.releaseMinMs = p.releaseMaxMs = 600.0;
    e.setParameters(p); e.reset();
    for (int n = 0; n < static_cast<int>(rate * 0.3); ++n) e.process(0.0f, 0.0f);
    CHECK(e.activeEvents() == 0, "TRANSIENTS never fires on digital silence");
    int maximum = 0;
    for (int n = 0; n < static_cast<int>(rate * 0.18); ++n) {
        // Multiple separated sharp pulses all fall within the first attack lockout.
        const float input = n % 900 < 40 ? 0.8f : 0.0f;
        e.process(input, input);
        maximum = std::max(maximum, static_cast<int>(e.activeEvents()));
    }
    CHECK(maximum == 1, "TRANSIENTS starts exactly one event during the attack lockout");
}

void testDryDuckingAndBypass() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.dryDb = 0.0;
    p.wetDb = -60.0;
    p.duckDryPercent = 100.0;
    p.attackMinMs = p.attackMaxMs = 10.0;
    p.releaseMinMs = p.releaseMaxMs = 500.0;
    e.setParameters(p); e.reset();
    for (int n = 0; n < static_cast<int>(rate * 0.1); ++n) e.process(0.2f, 0.2f);
    e.noteOn();
    double ducked = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 0.15); ++n) {
        const auto out = e.process(0.2f, 0.2f);
        if (n > rate * 0.04 && n < rate * 0.08) ducked += std::abs(out[0]);
    }
    const auto average = ducked / (rate * 0.04);
    CHECK(average < 0.12, "DUCK DRY attenuates the dry signal during the wet event");
    p.bypass = true;
    e.setParameters(p);
    std::array<float, 2> last {};
    for (int n = 0; n < static_cast<int>(rate * 0.15); ++n) last = e.process(0.2f, -0.13f);
    CHECK(std::abs(last[0] - 0.2f) < 1.0e-5f && std::abs(last[1] + 0.13f) < 1.0e-5f,
          "smoothed bypass settles to unchanged stereo dry audio");
}

void testPreviousEventDucking() {
    GrainEngine keep, duck;
    keep.prepare(rate); duck.prepare(rate);
    auto p = base();
    p.scatterMs = 60.0;
    p.attackMinMs = p.attackMaxMs = 10.0;
    p.releaseMinMs = p.releaseMaxMs = 1800.0;
    p.grainMinMs = p.grainMaxMs = 20.0;
    p.envMinPercent = p.envMaxPercent = 0.0;
    keep.setParameters(p); keep.reset();
    p.duckPreviousPercent = 100.0;
    duck.setParameters(p); duck.reset();
    double keepTail = 0.0, duckTail = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 1.0); ++n) {
        if (n == 0 || n == static_cast<int>(rate * 0.45)) { keep.noteOn(); duck.noteOn(); }
        // The second event captures silence, isolating the first event's remaining tail.
        const float input = n < rate * 0.15 ? 0.2f : 0.0f;
        const auto a = keep.process(input, input);
        const auto b = duck.process(input, input);
        if (n > rate * 0.65 && n < rate * 0.9) {
            keepTail += a[0] * a[0];
            duckTail += b[0] * b[0];
        }
    }
    CHECK(keepTail > 1.0e-3, "unducked earlier events retain their release tails");
    CHECK(duckTail < keepTail * 0.1, "DUCK PRV accelerates the earlier event's fade after a new trigger");
}

void testEventEnvelopeShape() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.scatterMs = 50.0;
    p.attackMinMs = p.attackMaxMs = 400.0;
    p.releaseMinMs = p.releaseMaxMs = 400.0;
    p.grainMinMs = p.grainMaxMs = 20.0;
    p.envMinPercent = p.envMaxPercent = 0.0;
    e.setParameters(p); e.reset(); e.noteOn();
    std::array<double, 7> amplitudes {};
    for (int n = 0; n < static_cast<int>(rate * 0.8); ++n) {
        const auto out = e.process(0.2f, 0.2f);
        for (int i = 0; i < 7; ++i) {
            const int center = static_cast<int>(rate * (0.1 * (i + 1)));
            if (std::abs(n - center) <= 12) amplitudes[static_cast<std::size_t>(i)] += out[0] / 25.0;
        }
    }
    CHECK(amplitudes[1] > 0.02, "event envelope test contains audible signal");
    CHECK(std::abs(amplitudes[0] * 2.0 - amplitudes[1]) < 0.015,
          "event attack rises linearly");
    CHECK(std::abs(amplitudes[0] * 3.0 - amplitudes[2]) < 0.02,
          "event attack keeps its linear slope toward the peak");
    CHECK(amplitudes[3] - amplitudes[4] > amplitudes[4] - amplitudes[5],
          "event release falls faster initially and slows toward silence");
}

void testAutomationContinuity() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.mode = 0;
    p.scatterMs = 100.0;
    p.grainMinMs = p.grainMaxMs = 20.0;
    p.envMinPercent = p.envMaxPercent = 0.0;
    e.setParameters(p); e.reset();
    float previous = 0.0f;
    double largestStep = 0.0, minSteady = 1.0, maxSteady = -1.0;
    for (int n = 0; n < static_cast<int>(rate * 1.5); ++n) {
        if (n == static_cast<int>(rate * 0.5)) {
            p.grainMinMs = p.grainMaxMs = 700.0;
            e.setParameters(p);
        }
        if (n == static_cast<int>(rate * 0.8)) {
            p.grainMinMs = p.grainMaxMs = 5.0;
            p.envMinPercent = p.envMaxPercent = 100.0;
            e.setParameters(p);
        }
        const auto out = e.process(0.2f, 0.2f);
        if (n > rate * 0.25 && n < rate * 0.45) {
            minSteady = std::min(minSteady, static_cast<double>(out[0]));
            maxSteady = std::max(maxSteady, static_cast<double>(out[0]));
        }
        if (n > rate * 0.3) largestStep = std::max(largestStep, std::abs(static_cast<double>(out[0] - previous)));
        previous = out[0];
    }
    CHECK(minSteady > 0.02, "50-percent overlap keeps constant captured audio continuous");
    CHECK(maxSteady - minSteady < 0.025, "triangle overlap avoids audible periodic gain pumping on DC");
    CHECK(largestStep < 0.03, "extreme grain and ENV automation does not make impulse-sized discontinuities");
}

void testShortCaptureLoopSeams() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.scatterMs = 17.0;
    p.grainMinMs = p.grainMaxMs = 500.0;
    p.attackMinMs = p.attackMaxMs = 10.0;
    p.releaseMinMs = p.releaseMaxMs = 1200.0;
    p.envMinPercent = p.envMaxPercent = 50.0;
    e.setParameters(p); e.reset(); e.noteOn();
    float previous = 0.0f;
    double largestStep = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 0.8); ++n) {
        // Deliberately non-periodic with respect to the 17 ms capture length.
        const float input = static_cast<float>(0.25 * std::sin(2.0 * pi * 123.0 * n / rate));
        const auto out = e.process(input, input);
        if (n > rate * 0.15) largestStep = std::max(largestStep, std::abs(static_cast<double>(out[0] - previous)));
        previous = out[0];
    }
    CHECK(largestStep < 0.08, "long grains smooth the repeated seams of a much shorter capture buffer");
}

double lagCorrelation(const std::vector<float>& audio, int begin, int end, int lag) {
    double cross = 0.0, a = 0.0, b = 0.0;
    for (int n = begin + lag; n < end; ++n) {
        const double x = audio[static_cast<std::size_t>(n)];
        const double y = audio[static_cast<std::size_t>(n - lag)];
        cross += x * y; a += x * x; b += y * y;
    }
    return cross / std::sqrt(std::max(1.0e-30, a * b));
}

void testTriggerDoesNotManufactureTinyLoopTones() {
    using Access = InhaGrain::GrainEngineTestAccess;
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.scatterMs = 2000.0f;
    p.grainMinMs = p.grainMaxMs = 1000.0f;
    p.attackMinMs = p.attackMaxMs = 40.0f;
    p.releaseMinMs = p.releaseMaxMs = 2000.0f;
    p.envMinPercent = p.envMaxPercent = 50.0f;
    p.duckPreviousPercent = 0.0f;
    const int total = static_cast<int>(rate * 0.4);
    std::vector<float> noise(static_cast<std::size_t>(total)), tone(noise.size());
    bool slices = true, silentBeforeCapture = true;
    for (int kind = 0; kind < 2; ++kind) {
        e.setParameters(p); e.reset(1234567); e.noteOn();
        std::uint32_t randomState = 0x217564a3u;
        for (int n = 0; n < total; ++n) {
            randomState ^= randomState << 13; randomState ^= randomState >> 17;
            randomState ^= randomState << 5;
            const auto input = kind == 0
                ? (static_cast<float>(randomState >> 8) / 8388608.0f - 1.0f) * 0.15f
                : static_cast<float>(0.15 * std::sin(2.0 * pi * 437.0 * n / rate));
            const auto out = e.process(input, input);
            (kind == 0 ? noise : tone)[static_cast<std::size_t>(n)] = out[0];
            slices = slices && Access::validSlices(e);
            if (n < rate * 0.04) silentBeforeCapture = silentBeforeCapture && out[0] == 0.0f;
        }
    }
    const int begin = static_cast<int>(rate * 0.06), end = static_cast<int>(rate * 0.3);
    double noiseCorrelation = 0.0;
    // The old 5 ms startup capture had a seam of min(2 ms, capture/4),
    // so its manufactured loop period depended on sample-rate rounding.
    for (int lag = static_cast<int>(rate * 0.0025); lag <= rate * 0.005; ++lag)
        noiseCorrelation = std::max(noiseCorrelation, std::abs(lagCorrelation(noise, begin, end, lag)));
    const auto toneCorrelation = lagCorrelation(tone, begin, end, static_cast<int>(std::lround(rate / 437.0)));
    CHECK(silentBeforeCapture, "new triggers wait for safe source capture instead of looping their first 5 ms");
    CHECK(slices, "every active grain reads a contiguous fully written source slice without internal wrapping");
    CHECK(std::abs(noiseCorrelation) < 0.25,
          "triggered broadband noise has no artificial stationary short-capture repetition tone");
    CHECK(toneCorrelation > 0.85,
          "off-grid 437 Hz input retains its pitch through grain onset instead of becoming a short-loop chord");
    CHECK(Access::parameters(e).grainMinMs == 1000.0f,
          "capture limits effective lengths without changing the requested GRAIN setting");
    std::printf("Onset regression: maximum noise short-loop correlation %.5f; 437Hz period correlation %.5f\n",
                noiseCorrelation, toneCorrelation);
}

void testMassPanAndCaptureBoundaries() {
    using Access = InhaGrain::GrainEngineTestAccess;
    GrainEngine single, dense, left, right;
    single.prepare(8000.0); dense.prepare(8000.0); left.prepare(8000.0); right.prepare(8000.0);
    auto p = base();
    p.mode = 0; p.scatterMs = 250.0f;
    p.grainMinMs = p.grainMaxMs = 100.0f;
    p.swayAmountMinPercent = p.swayAmountMaxPercent = 0.0f;
    p.attackMinMs = p.attackMaxMs = 40.0f;
    single.setParameters(p); single.reset();
    p.mass = 8; dense.setParameters(p); dense.reset();
    p.panMinPercent = p.panMaxPercent = -100.0f;
    left.setParameters(p); left.reset();
    p.panMinPercent = p.panMaxPercent = 100.0f;
    right.setParameters(p); right.reset();
    bool bounded = true, slices = true, stereo = true, correctSides = true;
    double levelDifference = 0.0;
    std::size_t peakGrains = 0;
    callbackAllocations = 0;
    trackAllocation = true;
    for (int n = 0; n < 8000 * 27; ++n) {
        const auto a = single.process(0.2f, -0.1f);
        const auto b = dense.process(0.2f, -0.1f);
        const auto l = left.process(0.2f, -0.1f);
        const auto r = right.process(0.2f, -0.1f);
        const auto active = Access::activeGrains(dense);
        peakGrains = std::max(peakGrains, active);
        bounded = bounded && active <= GrainEngine::maxGrainsPerEvent;
        slices = slices && Access::validSlices(dense);
        if (n > 800) {
            levelDifference = std::max(levelDifference, static_cast<double>(std::abs(a[0] - b[0])));
            stereo = stereo && std::abs(b[0] - 0.2f) < 0.002f && std::abs(b[1] + 0.1f) < 0.002f;
            correctSides = correctSides && std::abs(l[1]) < 1.0e-6f && std::abs(r[0]) < 1.0e-6f;
        }
    }
    trackAllocation = false;
    CHECK(peakGrains >= 12 && bounded, "MASS8 creates parallel overlapping streams within the fixed grain budget");
    CHECK(levelDifference < 0.002 && stereo,
          "increasing centered MASS preserves stereo channels and the original constant-input level");
    CHECK(correctSides, "PAN range endpoints pan individual grains fully to the selected stereo side");
    CHECK(slices, "live grains remain within written history after the capture ring wraps");
    CHECK(callbackAllocations.load() == 0, "MASS8 and PAN rendering allocate no audio-thread memory");

    p = base(); p.mode = 0; p.mass = 8; p.panMinPercent = -100; p.panMaxPercent = 100;
    p.motionMode = 0; p.duckPreviousPercent = 0;
    const auto first = deterministicRender(single, p, 64, 77123);
    p.motionMode = 1; p.undulationMinPercent = -100; p.undulationMaxPercent = 100;
    p.duckPreviousPercent = 100;
    const auto second = deterministicRender(single, p, 511, 77123);
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(Stereo)) == 0,
          "NONE always uses SWAY and ignores UNDULATION and DUCK PRV while retaining exact reset determinism");
    p = base(); p.durationMinimumMs = 15.0f; p.durationMaximumMs = 24000.0f;
    p.scatterMs = 24000.0f; p.grainMinMs = p.grainMaxMs = 15000.0f;
    p.attackMinMs = p.attackMaxMs = 15.0f;
    dense.setParameters(p);
    const auto synced = Access::parameters(dense);
    CHECK(synced.grainMinMs == 15000.0f && synced.attackMinMs == 15.0f,
          "sync-resolved durations retain their expanded millisecond domain");
}

void testFrozenBufferAutomationAndDirtyResets() {
    using Access = InhaGrain::GrainEngineTestAccess;
    GrainEngine e;
    e.prepare(rate);
    auto p = base(); p.mode = 3;
    p.scatterMs = 500.0f; p.grainMinMs = p.grainMaxMs = 300.0f;
    p.attackMinMs = p.attackMaxMs = 40.0f;
    e.setParameters(p); e.reset();
    for (int n = 0; n < static_cast<int>(rate); ++n) e.process(0.2f, 0.2f);
    auto previous = Access::firstEvent(e);
    p.scatterMs = 40.0f;
    e.setParameters(p);
    bool capped = true;
    int newGrains = 0;
    for (int n = 0; n < static_cast<int>(rate * 0.4); ++n) {
        e.process(0.2f, 0.2f);
        const auto current = Access::firstEvent(e);
        if (current.grainSerial != previous.grainSerial) {
            ++newGrains;
            capped = capped && current.previousGrain <= rate * 0.04 + 1;
        }
        previous = current;
    }
    CHECK(newGrains >= 3 && capped,
          "lowering BUFFER caps newly launched grains in an already-frozen held event");
    CHECK(Access::parameters(e).grainMinMs == 300.0f,
          "BUFFER automation preserves the requested GRAIN value");
    p = base(); p.mass = 8; p.panMinPercent = -100; p.panMaxPercent = 100;
    e.setParameters(p); e.reset(12345); e.noteOn();
    bool silent = true;
    for (int n = 0; n < static_cast<int>(rate); ++n) {
        const auto out = e.process(0.0f, 0.0f);
        silent = silent && out[0] == 0.0f && out[1] == 0.0f;
    }
    CHECK(silent, "reset with previously filled capture storage cannot leak stale audio into silence");
    p.mode = 0; p.mass = 1;
    const auto sparse = deterministicRender(e, p, 64, 778899);
    p.mass = 8;
    const auto dense = deterministicRender(e, p, 127, 778899);
    double difference = 0.0;
    for (std::size_t n = 0; n < sparse.size(); ++n)
        difference += std::abs(sparse[n].left - dense[n].left);
    CHECK(difference > 1.0,
          "parallel MASS streams choose independent source starts and audibly change nonconstant input");
}

void testSyncPicksStayOnMusicalGrid() {
    using Access = InhaGrain::GrainEngineTestAccess;
    namespace Timing = InhaGrain::Timing;
    GrainEngine e;
    constexpr double lowRate = 8000.0;
    e.prepare(lowRate);
    bool eventGrid = true, grainGrid = true, slices = true;
    int launched = 0;
    for (const float bpm : { 20.0f, 123.0f, 999.0f }) {
        auto p = base(); p.synced = true; p.tempoBpm = bpm; p.mass = 8;
        p.durationMinimumMs = static_cast<float>(Timing::minimumDuration(true, bpm));
        p.durationMaximumMs = static_cast<float>(Timing::maximumDuration(true, bpm));
        p.scatterMs = p.durationMaximumMs;
        p.attackMinMs = p.releaseMinMs = p.grainMinMs = p.durationMinimumMs;
        p.attackMaxMs = p.releaseMaxMs = p.grainMaxMs = p.durationMaximumMs;
        e.setParameters(p); e.reset(123456); e.noteOn();
        const auto onGrid = [bpm, lowRate](std::uint64_t frames) {
            for (std::size_t step = 0; step < Timing::beats.size(); ++step) {
                const auto grid = static_cast<std::uint64_t>(std::llround(
                    Timing::syncMilliseconds(Timing::syncNormalized(step), bpm) * lowRate * 0.001));
                if (grid == frames) return true;
            }
            return false;
        };
        auto previous = Access::firstEvent(e);
        for (int n = 0; n < 24000; ++n) {
            e.process(0.2f, 0.2f);
            const auto current = Access::firstEvent(e);
            if (current.active) eventGrid = eventGrid && onGrid(current.attack) && onGrid(current.release);
            if (current.active && current.grainSerial != previous.grainSerial) {
                ++launched;
                grainGrid = grainGrid && onGrid(current.previousGrain);
            }
            slices = slices && Access::validSlices(e);
            previous = current;
        }
    }
    CHECK(launched > 20 && eventGrid,
          "SYNC random attack/release picks select actual musical grid durations at slow and fast tempos");
    CHECK(grainGrid && slices,
          "SYNC grains stay on musical durations while startup capture and BUFFER cap their effective length");
}

void testLongUnequalGrainOverlap() {
    GrainEngine e;
    constexpr double lowRate = 8000.0;
    e.prepare(lowRate);
    auto p = base();
    p.mode = 0;
    p.attackMinMs = p.attackMaxMs = 1.0;
    p.grainMinMs = 5.0;
    p.grainMaxMs = 10000.0;
    p.envMinPercent = p.envMaxPercent = 0.0;
    p.undulationMinPercent = p.undulationMaxPercent = 0.0;
    p.scatterMs = 6000.0;
    p.mass = 8;
    e.setParameters(p); e.reset();
    float previous = 0.2f;
    double minimum = 1.0, maximum = -1.0, largestStep = 0.0;
    for (int n = 0; n < static_cast<int>(lowRate * 90.0); ++n) {
        if (n > lowRate * 10.0 && n % 35231 == 0) {
            // Unequal old/new grain lengths expose window sums below unity.
            // Alternating the whole domain also exercises the safe duration slew.
            const int change = n / 35231;
            p.grainMinMs = change % 3 == 0 ? 5.0 : (change % 3 == 1 ? 10000.0 : 100.0);
            p.grainMaxMs = change % 3 == 0 ? 5.0 : (change % 3 == 1 ? 10000.0 : 9000.0);
            p.envMinPercent = p.envMaxPercent = change % 2 ? 0.0 : 100.0;
            e.setParameters(p);
        }
        const auto out = e.process(0.2f, 0.2f);
        if (n > lowRate * 0.1) {
            minimum = std::min(minimum, static_cast<double>(out[0]));
            maximum = std::max(maximum, static_cast<double>(out[0]));
            largestStep = std::max(largestStep, std::abs(static_cast<double>(out[0] - previous)));
        }
        previous = out[0];
    }
    CHECK(minimum > 0.198 && maximum < 0.202,
          "90-second unequal-duration overlap preserves DC gain through extreme GRAIN/ENV changes");
    CHECK(largestStep < 0.002,
          "normalizing short and long overlapping grains never adds amplitude steps");
}

void testBypassPreservesInterstagePeaks() {
    GrainEngine e;
    e.prepare(rate);
    auto p = base();
    p.bypass = true;
    p.dryDb = -60.0;
    p.wetDb = 6.0;
    p.duckDryPercent = 100.0;
    e.setParameters(p); e.reset();
    const std::array<float, 6> peaks {{ 1.2f, -1.8f, 0.12345f, 4.0f, -0.333f, -7.0f }};
    bool exact = true;
    for (const auto peak : peaks) {
        const auto out = e.process(peak, -peak);
        exact = exact && out[0] == peak && out[1] == -peak;
    }
    CHECK(exact, "full bypass preserves exact dry samples above 0 dBFS instead of clipping them");
    p.bypass = false;
    e.setParameters(p);
    for (int n = 0; n < 4000; ++n) e.process(1.2f, -1.8f);
    p.bypass = true;
    e.setParameters(p);
    std::array<float, 2> settled {};
    for (int n = 0; n < 8000; ++n) settled = e.process(1.2f, -1.8f);
    CHECK(settled[0] == 1.2f && settled[1] == -1.8f,
          "automated bypass settles to exact dry peaks after its smooth transition");
}

void testEventGrainQuantileAndUndulation() {
    using Access = InhaGrain::GrainEngineTestAccess;
    constexpr double lowRate = 8000.0;
    GrainEngine e;
    e.prepare(lowRate);
    auto p = base();
    p.mode = 3; // Held triggered event permits UNDULATION.
    p.scatterMs = 2000.0;
    p.swayAmountMinPercent = p.swayAmountMaxPercent = 0.0f;
    p.grainMinMs = 100.0;
    p.grainMaxMs = 400.0;
    p.attackMinMs = p.attackMaxMs = 500.0;
    p.releaseMinMs = p.releaseMaxMs = 1500.0;
    p.undulationMinPercent = p.undulationMaxPercent = 0.0;
    p.envMinPercent = p.envMaxPercent = 0.0;
    e.setParameters(p); e.reset();
    auto before = Access::firstEvent(e);
    bool fixedQuantile = true, mappedSize = true;
    float quantile = -1.0f;
    int observedGrains = 0;
    for (int n = 0; n < static_cast<int>(lowRate * 3.0); ++n) {
        if (n == static_cast<int>(lowRate)) {
            p.grainMinMs = 200.0; p.grainMaxMs = 500.0;
            e.setParameters(p);
        }
        e.process(0.2f, 0.2f);
        const auto after = Access::firstEvent(e);
        if (after.grainSerial != before.grainSerial) {
            if (quantile < 0.0f) quantile = after.grainQuantile;
            fixedQuantile = fixedQuantile && quantile == after.grainQuantile;
            const double expected = std::min<double>(after.written * 1000.0 / lowRate,
                p.grainMinMs + (p.grainMaxMs - p.grainMinMs) * quantile);
            const double actual = after.previousGrain * 1000.0 / lowRate;
            mappedSize = mappedSize && std::abs(expected - actual) <= 0.13;
            ++observedGrains;
        }
        before = after;
    }
    CHECK(observedGrains >= 8, "quantile regression observes several grains before and after automation");
    CHECK(fixedQuantile, "each event retains its seeded grain-size choice across successive grains");
    CHECK(mappedSize, "automating GRAIN remaps the existing event quantile instead of rerolling it");

    // With modest undulation the duration slew does not obscure the requested
    // target. Check the actual launched lengths against A+R times SPEED.
    bool durationMapping = true, reachedDestination = true;
    for (const float speed : { 50.0f, 100.0f, 200.0f }) {
        p.grainMinMs = p.grainMaxMs = 500.0;
        p.undulationMinPercent = p.undulationMaxPercent = 5.0;
        p.speedMinPercent = p.speedMaxPercent = speed;
        e.setParameters(p); e.reset();
        before = Access::firstEvent(e);
        bool reached = false;
        for (int n = 0; n < static_cast<int>(lowRate * 7.0); ++n) {
            e.process(0.2f, 0.2f);
            const auto after = Access::firstEvent(e);
            if (after.grainSerial != before.grainSerial) {
                const double durationSamples = (after.attack + after.release) * (speed * 0.01);
                const double progress = std::clamp((after.age - 1.0) / durationSamples, 0.0, 1.0);
                const double expected = std::min<double>(after.written * 1000.0 / lowRate,
                    500.0 + (6000.0 - 500.0) * 0.05 * progress);
                const double actual = after.previousGrain * 1000.0 / lowRate;
                durationMapping = durationMapping && std::abs(expected - actual) <= 0.13;
                if (progress == 1.0) {
                    reached = true;
                    durationMapping = durationMapping && std::abs(actual - 775.0) <= 0.13;
                }
            }
            before = after;
        }
        reachedDestination = reachedDestination && reached;
    }
    CHECK(durationMapping,
          "UNDULATION duration is A+R multiplied by SPEED50/100/200 percent, with global-domain amount");
    CHECK(reachedDestination, "sustained events reach the destination after each requested undulation duration");

    bool directionCorrect = true;
    p.grainMinMs = 100.0f; p.grainMaxMs = 500.0f;
    for (const float amount : { -100.0f, 100.0f }) {
        p.undulationMinPercent = p.undulationMaxPercent = amount;
        p.speedMinPercent = p.speedMaxPercent = 100.0;
        e.setParameters(p); e.reset();
        before = Access::firstEvent(e);
        for (int n = 0; n < static_cast<int>(lowRate * 6.0); ++n) {
            e.process(0.2f, 0.2f);
            const auto after = Access::firstEvent(e);
            if (after.grainSerial != before.grainSerial && before.previousGrain != 0 &&
                after.age > lowRate * 0.6) {
                directionCorrect = directionCorrect && (amount < 0.0f
                    ? after.previousGrain <= before.previousGrain
                    : after.previousGrain >= before.previousGrain);
            }
            before = after;
        }
    }
    CHECK(directionCorrect, "signed UNDULATION changes grain size monotonically in the selected direction");
}

void testUndulationUsesSelectedGrainMinimum() {
    using Access = InhaGrain::GrainEngineTestAccess;
    namespace Timing = InhaGrain::Timing;
    constexpr double lowRate = 8000.0;
    GrainEngine e;
    e.prepare(lowRate);
    struct Result { std::uint32_t endpoint = 0; float quantile = 0.0f; };
    const auto render = [&](float lower, float upper, float amount, float buffer,
                            bool synced, float raisedMinimum = 0.0f) {
        auto p = base();
        p.mode = 3; p.motionMode = 0;
        p.scatterMs = buffer;
        p.grainMinMs = lower; p.grainMaxMs = upper;
        p.attackMinMs = p.attackMaxMs = 300.0f;
        p.releaseMinMs = p.releaseMaxMs = 700.0f;
        p.speedMinPercent = p.speedMaxPercent = 100.0f;
        p.undulationMinPercent = p.undulationMaxPercent = amount;
        p.synced = synced; p.tempoBpm = 120.0f;
        if (synced) {
            p.durationMinimumMs = static_cast<float>(Timing::minimumDuration(true, p.tempoBpm));
            p.durationMaximumMs = static_cast<float>(Timing::maximumDuration(true, p.tempoBpm));
        }
        e.setParameters(p); e.reset(987654);
        auto previous = Access::firstEvent(e);
        bool floorHolds = true, valid = true, raisedImmediately = raisedMinimum == 0.0f;
        Result result;
        for (int n = 0; n < static_cast<int>(lowRate * 8.0); ++n) {
            if (raisedMinimum != 0.0f && n == static_cast<int>(lowRate * 3.0)) {
                p.grainMinMs = raisedMinimum;
                e.setParameters(p);
            }
            e.process(0.2f, 0.2f);
            const auto current = Access::firstEvent(e);
            if (current.grainSerial != previous.grainSerial) {
                const auto selected = std::min(p.grainMinMs, p.grainMaxMs);
                const auto floorMs = synced
                    ? Timing::quantizeMilliseconds(selected, p.tempoBpm) : selected;
                const auto floor = static_cast<std::uint64_t>(std::llround(floorMs * lowRate * 0.001));
                const auto available = std::min(current.written,
                    static_cast<std::uint64_t>(std::llround(buffer * lowRate * 0.001)));
                if (available >= floor)
                    floorHolds = floorHolds && current.previousGrain >= floor;
                if (raisedMinimum != 0.0f && n >= lowRate * 3.0 && !raisedImmediately) {
                    raisedImmediately = current.previousGrain >= floor;
                    CHECK(raisedImmediately,
                          "automating the UNDULATION minimum upward applies its floor to the next grain despite duration slew");
                }
                result = { current.previousGrain, current.grainQuantile };
            }
            valid = valid && Access::validSlices(e);
            previous = current;
        }
        CHECK(floorHolds, "UNDULATION in either direction never undershoots the selected GRAIN minimum once captured");
        CHECK(valid, "selected-minimum UNDULATION retains contiguous fully captured source reads");
        CHECK(raisedImmediately, "minimum automation regression observes a newly launched grain");
        return result;
    };
    const auto small = render(250.0f, 1800.0f, -100.0f, 6000.0f, false);
    const auto large = render(900.0f, 1800.0f, -100.0f, 6000.0f, false);
    const auto reversed = render(1800.0f, 900.0f, -100.0f, 6000.0f, false);
    CHECK(small.endpoint == 2000 && large.endpoint == 7200 && reversed.endpoint == 7200,
          "falling UNDULATION ends at the selected250/900ms GRAIN minimum and handles reversed range endpoints");
    CHECK(small.quantile == large.quantile && large.quantile == reversed.quantile,
          "changing the UNDULATION floor preserves the event's seeded random starting quantile");
    const auto capped = render(900.0f, 1800.0f, -100.0f, 300.0f, false);
    CHECK(capped.endpoint == 2400, "BUFFER shorter than the selected minimum still caps effective UNDULATION grain length");
    const auto rising = render(900.0f, 1800.0f, 100.0f, 6000.0f, false);
    CHECK(rising.endpoint == 48000, "rising UNDULATION retains the existing upper timing-domain destination");
    const auto automated = render(250.0f, 1800.0f, -100.0f, 6000.0f, false, 900.0f);
    CHECK(automated.endpoint == 7200, "raising the selected minimum updates an already-running falling UNDULATION endpoint");
    const auto synced = render(500.0f, 2000.0f, -100.0f, 4000.0f, true, 2000.0f);
    CHECK(synced.endpoint == 16000, "SYNC falling UNDULATION uses the selected musical GRAIN minimum after automation");
    render(900.0f, 1800.0f, 40.0f, 6000.0f, false);
}

void testSwayTimelineAndRange() {
    using Access = InhaGrain::GrainEngineTestAccess;
    GrainEngine a, b;
    constexpr double lowRate = 8000.0;
    a.prepare(lowRate); b.prepare(lowRate);
    auto p = base();
    p.mode = 2;
    p.motionMode = 1;
    p.swayAmountMinPercent = 10.0f; p.swayAmountMaxPercent = 30.0f;
    p.swaySpeedMinMs = 500.0f; p.swaySpeedMaxMs = 1500.0f;
    a.setParameters(p); b.setParameters(p);
    a.reset(0); b.reset(987654);
    bool identical = true, bounded = true;
    float minimum = 1.0f, maximum = -1.0f;
    for (int i = -500; i < 2500; ++i) {
        const std::int64_t sample = static_cast<std::int64_t>(i) * 997;
        const float x = Access::swayAt(a, sample);
        const float y = Access::swayAt(b, sample);
        identical = identical && x == y;
        bounded = bounded && std::isfinite(x) && std::abs(x) <= 0.300001f;
        minimum = std::min(minimum, x); maximum = std::max(maximum, x);
    }
    CHECK(identical, "SWAY seed/timeline lookup is identical regardless of current playback position");
    CHECK(bounded && minimum < -0.03f && maximum > 0.03f,
          "SWAY moves in both directions within its selected maximum depth");

    double seedDifference = 0.0;
    ++p.seed;
    b.setParameters(p);
    for (int i = 0; i < 1000; ++i) {
        const std::int64_t sample = static_cast<std::int64_t>(i) * 991;
        seedDifference += std::abs(Access::swayAt(a, sample) - Access::swayAt(b, sample));
    }
    CHECK(seedDifference > 1.0, "refreshing the seed changes the continuous SWAY path");
    --p.seed;
    b.setParameters(p);
    const std::array<std::int64_t, 7> seeks {{
        0, -1, 57, 987654, -1234567, (std::int64_t{1} << 42) + 12345,
        -(std::int64_t{1} << 40) + 987
    }};
    bool seekMatches = true;
    for (const auto sample : seeks) {
        const float expected = Access::swayAt(a, sample);
        b.reset(sample);
        seekMatches = seekMatches && Access::currentSway(b) == expected;
    }
    CHECK(seekMatches, "arbitrary positive/negative timeline resets recover the same SWAY phase directly");

    // The random clock must not follow event age, attack, release, or trigger count.
    p.attackMinMs = p.attackMaxMs = 1.0f;
    p.releaseMinMs = p.releaseMaxMs = 5.0f;
    a.setParameters(p); a.reset();
    p.attackMinMs = p.attackMaxMs = 10000.0f;
    p.releaseMinMs = p.releaseMaxMs = 30000.0f;
    b.setParameters(p); b.reset();
    bool independent = true;
    for (int n = 0; n < 24000; ++n) {
        if (n % 97 == 0) a.noteOn();
        if (n % 2909 == 0) b.noteOn();
        independent = independent && Access::currentSway(a) == Access::currentSway(b);
        a.process(0.2f, 0.2f); b.process(0.2f, 0.2f);
    }
    CHECK(independent, "SWAY global time survives event retriggers and is independent of attack/release");
}

void testSwayAudioDeterminism() {
    GrainEngine a, b;
    a.prepare(rate); b.prepare(rate);
    auto p = base();
    p.motionMode = 1;
    p.swayAmountMinPercent = 10.0f; p.swayAmountMaxPercent = 70.0f;
    p.swaySpeedMinMs = 25.0f; p.swaySpeedMaxMs = 500.0f;
    const auto reference = deterministicRender(a, p, 64, 9876543);
    const auto differentBlocks = deterministicRender(b, p, 511, 9876543);
    const auto replay = deterministicRender(a, p, 127, 9876543);
    CHECK(std::memcmp(reference.data(), differentBlocks.data(), reference.size() * sizeof(Stereo)) == 0,
          "nonzero SWAY audio is identical across different host block partitions");
    CHECK(std::memcmp(reference.data(), replay.data(), reference.size() * sizeof(Stereo)) == 0,
          "SWAY audio replays exactly after resetting the same seed and timeline");

    // In UNDULATION mode, unused SWAY settings must not disturb established projects.
    p.motionMode = 0;
    p.undulationMinPercent = -30.0f; p.undulationMaxPercent = 60.0f;
    const auto under = deterministicRender(a, p, 64, 1234567);
    p.swayAmountMinPercent = 0.0f; p.swayAmountMaxPercent = 100.0f;
    p.swaySpeedMinMs = 20000.0f; p.swaySpeedMaxMs = 20000.0f;
    const auto underWithOtherSway = deterministicRender(b, p, 511, 1234567);
    CHECK(std::memcmp(under.data(), underWithOtherSway.data(), under.size() * sizeof(Stereo)) == 0,
          "unused SWAY parameters leave original UNDULATION audio unchanged");
}

void testSwaySpeedAndSmoothness() {
    using Access = InhaGrain::GrainEngineTestAccess;
    GrainEngine e;
    constexpr double lowRate = 8000.0;
    e.prepare(lowRate);
    auto p = base();
    p.motionMode = 1;
    p.swayAmountMinPercent = 10.0f; p.swayAmountMaxPercent = 30.0f;
    bool intervalBounds = true, smooth = true;
    for (const auto range : { std::array<float, 2>{{ 25.0f, 20000.0f }},
                              std::array<float, 2>{{ 500.0f, 1500.0f }},
                              std::array<float, 2>{{ 1200.0f, 1200.0f }} }) {
        p.swaySpeedMinMs = range[0]; p.swaySpeedMaxMs = range[1];
        e.setParameters(p);
        for (int n = -256; n <= 256; ++n) {
            const double time = Access::swayKnotAt(e, n);
            const double next = Access::swayKnotAt(e, n + 1);
            const double interval = next - time;
            intervalBounds = intervalBounds && interval >= range[0] - 0.001 &&
                interval <= range[1] + 0.001 && interval > 0.0;
            if (range[0] == range[1])
                intervalBounds = intervalBounds && std::abs(interval - range[0]) < 0.001;
            const auto sample = static_cast<std::int64_t>(std::llround(time * lowRate * 0.001));
            for (int offset = -3; offset <= 3; ++offset) {
                const float previous = Access::swayAt(e, sample + offset - 1);
                const float current = Access::swayAt(e, sample + offset);
                smooth = smooth && std::isfinite(current) && std::abs(current - previous) < 0.01f;
            }
        }
    }
    CHECK(intervalBounds, "every SWAY change interval lies inside the SPEED millisecond range");
    CHECK(smooth, "SWAY joins its seeded targets smoothly at random clock boundaries");
}

void testSwayZeroDepthAndBaseAutomation() {
    using Access = InhaGrain::GrainEngineTestAccess;
    GrainEngine under, sway;
    under.prepare(rate); sway.prepare(rate);
    auto p = base();
    p.undulationMinPercent = p.undulationMaxPercent = 0.0f;
    p.motionMode = 0;
    const auto steady = deterministicRender(under, p, 64, 7654321);
    p.motionMode = 1;
    p.swayAmountMinPercent = p.swayAmountMaxPercent = 0.0f;
    const auto zeroDepth = deterministicRender(sway, p, 511, 7654321);
    CHECK(std::memcmp(steady.data(), zeroDepth.data(), steady.size() * sizeof(Stereo)) == 0,
          "zero SWAY amount renders exactly the same audio as zero UNDULATION");

    constexpr double lowRate = 8000.0;
    sway.prepare(lowRate);
    p.mode = 0;
    p.scatterMs = 2000.0f;
    p.attackMinMs = p.attackMaxMs = 40.0f;
    p.grainMinMs = p.grainMaxMs = 400.0f;
    p.swayAmountMinPercent = p.swayAmountMaxPercent = 20.0f;
    p.swaySpeedMinMs = 40.0f; p.swaySpeedMaxMs = 80.0f;
    sway.setParameters(p);
    constexpr std::int64_t origin = 123456;
    sway.reset(origin);
    auto previous = Access::firstEvent(sway);
    bool centered = true;
    int beforeAutomation = 0, afterAutomation = 0;
    for (int n = 0; n < static_cast<int>(lowRate * 6.0); ++n) {
        if (n == static_cast<int>(lowRate * 2.0)) {
            p.grainMinMs = p.grainMaxMs = 500.0f;
            sway.setParameters(p);
        }
        const double desired = std::min((n + 1.0) * 1000.0 / lowRate,
            p.grainMinMs * (1.0 + Access::swayAt(sway, origin + n)));
        sway.process(0.2f, 0.2f);
        const auto event = Access::firstEvent(sway);
        if (event.grainSerial != previous.grainSerial) {
            const double actual = event.previousGrain * 1000.0 / lowRate;
            centered = centered && std::abs(actual - desired) <= 0.13;
            if (n < lowRate * 2.0) ++beforeAutomation; else ++afterAutomation;
        }
        previous = event;
    }
    CHECK(beforeAutomation >= 4 && afterAutomation >= 8,
          "SWAY center test observes grains before and after GRAIN automation");
    CHECK(centered, "SWAY depth remains relative to the current automated grain size");
}

void testSwayAutomationSafety() {
    GrainEngine e;
    constexpr double lowRate = 8000.0;
    e.prepare(lowRate);
    auto p = base();
    p.mode = 0;
    p.attackMinMs = p.attackMaxMs = 1.0f;
    p.grainMinMs = 20.0f; p.grainMaxMs = 80.0f;
    p.envMinPercent = 0.0f; p.envMaxPercent = 100.0f;
    p.motionMode = 1;
    e.setParameters(p); e.reset();
    bool finite = true, fixedBudget = true;
    double minimum = 1.0, maximum = -1.0, largestStep = 0.0;
    float previous = 0.2f;
    const auto memory = e.memoryBytes();
    callbackAllocations = 0;
    trackAllocation = true;
    for (int n = 0; n < 80000; ++n) {
        if (n % 47 == 0) {
            p.motionMode = (n / 47) % 2;
            p.swayAmountMinPercent = (n / 47) % 3 ? 0.0f : 100.0f;
            p.swayAmountMaxPercent = (n / 47) % 5 ? 100.0f : 0.0f;
            p.swaySpeedMinMs = (n / 47) % 3 ? 25.0f : 20000.0f;
            p.swaySpeedMaxMs = (n / 47) % 5 ? 20000.0f : 25.0f;
            e.setParameters(p);
        }
        const auto out = e.process(0.2f, 0.2f);
        finite = finite && std::isfinite(out[0]) && std::isfinite(out[1]);
        fixedBudget = fixedBudget && e.activeEvents() == 1;
        if (n > 800) {
            minimum = std::min(minimum, static_cast<double>(out[0]));
            maximum = std::max(maximum, static_cast<double>(out[0]));
            largestStep = std::max(largestStep, std::abs(static_cast<double>(out[0] - previous)));
        }
        previous = out[0];
    }
    trackAllocation = false;
    CHECK(finite && minimum > 0.198 && maximum < 0.202 && largestStep < 0.002,
          "rapid SWAY AMOUNT/SPEED/tab automation adds no DC tremolo, clicks, or non-finite output");
    CHECK(fixedBudget && e.memoryBytes() == memory,
          "SWAY tab and parameter changes keep the existing capture event and fixed resource budget");
    CHECK(callbackAllocations.load() == 0, "SWAY clock and automation allocate no audio-thread memory");
}

void testFloodFiniteAndNoAllocations() {
    GrainEngine e;
    e.prepare(48000.0);
    auto p = base();
    p.scatterMs = 20000.0;
    p.attackMinMs = 1.0; p.attackMaxMs = 10000.0;
    p.releaseMinMs = 10000.0; p.releaseMaxMs = 30000.0;
    p.grainMinMs = 5.0; p.grainMaxMs = 10000.0;
    p.envMinPercent = 0.0; p.envMaxPercent = 100.0;
    p.undulationMinPercent = -100.0; p.undulationMaxPercent = 100.0;
    p.duckPreviousPercent = 100.0;
    p.dryDb = 6.0; p.wetDb = 6.0;
    e.setParameters(p); e.reset();
    bool finite = true, bounded = true, budget = true;
    callbackAllocations = 0;
    const auto memoryBefore = e.memoryBytes();
    trackAllocation = true;
    for (int n = 0; n < 96000; ++n) {
        if (n % 11 == 0) e.noteOn();
        if (n % 47 == 0) {
            p.grainMinMs = n % 2 ? 5.0 : 3000.0;
            p.grainMaxMs = n % 3 ? 10000.0 : 5.0; // also exercises crossed ranges
            e.setParameters(p);
        }
        float in = 0.7f * std::sin(static_cast<float>(n) * 0.041f);
        if (n == 13001) in = std::numeric_limits<float>::quiet_NaN();
        if (n == 19001) in = std::numeric_limits<float>::infinity();
        const auto out = e.process(in, -in);
        finite = finite && std::isfinite(out[0]) && std::isfinite(out[1]);
        bounded = bounded && std::abs(out[0]) <= 1.00001f && std::abs(out[1]) <= 1.00001f;
        budget = budget && e.activeEvents() <= GrainEngine::maxEvents;
    }
    trackAllocation = false;
    CHECK(finite, "flooded triggers, hostile input, and automation always produce finite output");
    CHECK(bounded, "event pressure and wet safety prevent runaway output gain");
    CHECK(budget, "event count always stays within the fixed resource budget");
    CHECK(callbackAllocations.load() == 0, "process, noteOn, and setParameters never allocate");
    CHECK(e.memoryBytes() == memoryBefore, "worst-case playback never expands capture memory");
}
}

int main() {
    testDeterminism();
    testMidiProbabilityAndTail();
    testFrozenCaptureAndLiveCapture();
    testVolumeHoldAndRelease();
    testTransientSilenceAndLockout();
    testDryDuckingAndBypass();
    testPreviousEventDucking();
    testEventEnvelopeShape();
    testAutomationContinuity();
    testShortCaptureLoopSeams();
    testTriggerDoesNotManufactureTinyLoopTones();
    testMassPanAndCaptureBoundaries();
    testFrozenBufferAutomationAndDirtyResets();
    testSyncPicksStayOnMusicalGrid();
    testLongUnequalGrainOverlap();
    testBypassPreservesInterstagePeaks();
    testEventGrainQuantileAndUndulation();
    testUndulationUsesSelectedGrainMinimum();
    testSwayTimelineAndRange();
    testSwayAudioDeterminism();
    testSwaySpeedAndSmoothness();
    testSwayZeroDepthAndBaseAutomation();
    testSwayAutomationSafety();
    testFloodFiniteAndNoAllocations();
    if (failures != 0) {
        std::fprintf(stderr, "%d Grain engine check(s) failed.\n", failures);
        return 1;
    }
    std::puts("All Grain engine checks passed.");
    return 0;
}
