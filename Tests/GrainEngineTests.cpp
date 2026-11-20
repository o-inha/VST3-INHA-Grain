#include "GrainEngine.h"

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
        bounded = bounded && std::abs(out[0]) <= 4.0f && std::abs(out[1]) <= 4.0f;
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
    testFloodFiniteAndNoAllocations();
    if (failures != 0) {
        std::fprintf(stderr, "%d Grain engine check(s) failed.\n", failures);
        return 1;
    }
    std::puts("All Grain engine checks passed.");
    return 0;
}
