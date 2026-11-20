#include "GrainTiming.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require (bool value, const char* message)
{
    if (!value) throw std::runtime_error (message);
}
void near (double value, double expected, const char* message)
{
    require (std::abs (value - expected) < 1.0e-8, message);
}
}

int main()
{
    using namespace InhaGrain::Timing;
    try
    {
        near (minimumDuration (false, 80), 40, "Free duration starts at 40 ms");
        near (maximumDuration (false, 80), 6000, "Free duration ends at two 4/4 bars at 80 BPM");
        near (syncMilliseconds (0, 80), 187.5, "Sixteenth note at 80 BPM");
        near (syncMilliseconds (1, 80), 6000, "Two 4/4 bars at 80 BPM");
        near (syncMilliseconds (0.4, 120), 500, "Quarter note at 120 BPM");
        near (syncMilliseconds (0.6, 120), 1000, "Half note at 120 BPM");
        near (syncMilliseconds (0.8, 90), 8000.0 / 3.0, "One 4/4 bar at 90 BPM");
        near (syncMilliseconds (1, 20), 24000, "Slow supported tempo fits preallocated capture");
        near (quantizeMilliseconds (800, 120), 1000, "Chosen random duration resolves to a musical note");
        near (floorMilliseconds (800, 120), 500, "Capture-limited grain uses a completely captured musical length");
        require (maximumDuration (true, minimumTempo) <= maximumCaptureMilliseconds,
                 "Sync cannot request more than capture storage");
        near (tempo (std::numeric_limits<double>::quiet_NaN()), 120, "Invalid tempo falls back to 120 BPM");
        near (tempo (0), 120, "Missing tempo falls back to 120 BPM");
        near (tempo (-80), 120, "Negative tempo falls back to 120 BPM");
        require (std::wstring (syncLabel (0.0)) == L"1/16", "Shortest musical label is a sixteenth note");
        near (syncNormalizedFromLabel (L"1/4"), 0.4, "Quarter-note text preserves musical value");
        near (syncNormalizedFromLabel (L"2 BARS"), 1.0, "Two-bar text preserves musical value");
        require (syncNormalizedFromLabel (L"invalid") < 0, "Invalid musical text is rejected");
        require (syncIndex (-1) == 0 && syncIndex (2) == 5, "Automated sync positions are bounded");
        const std::array<double, 11> mixedBeats {{
            0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0,
            2.0, 8.0 / 3.0, 4.0, 16.0 / 3.0, 8.0
        }};
        const std::array<const wchar_t*, 11> mixedLabels {{
            L"1/16", L"1/8T", L"1/8", L"1/4T", L"1/4", L"1/2T",
            L"1/2", L"1/1T", L"1 BAR", L"2/1T", L"2 BARS"
        }};
        require (choiceCount (false) == 6 && choiceCount (true) == mixedBeats.size(),
                 "T expands the existing straight pool with five triplet choices");
        for (std::size_t i = 0; i < mixedBeats.size(); ++i)
        {
            const auto position = syncNormalized (i, true);
            require (syncIndex (position, true) == i, "Mixed-pool positions round-trip");
            near (choiceBeats (i, true), mixedBeats[i], "Mixed pool keeps straight and triplet notes in time order");
            near (syncMilliseconds (position, 120, true), mixedBeats[i] * 500.0,
                  "Triplet and straight labels resolve to their exact musical durations");
            require (std::wstring (syncLabel (position, true)) == mixedLabels[i],
                     "Mixed-pool label matches its note duration");
            near (syncNormalizedFromLabel (mixedLabels[i], true), position,
                  "Mixed musical text preserves its requested choice");
            near (quantizeMilliseconds (mixedBeats[i] * 500.0, 120, true), mixedBeats[i] * 500.0,
                  "Random duration snapping can select every straight and triplet choice");
        }
        for (std::size_t i = 0; i < beats.size(); ++i)
        {
            const auto position = syncNormalized (i);
            near (syncNormalized (i, false), position, "T off preserves original normalized positions");
            near (syncNormalized (i, false), syncNormalized (2 * i, true),
                  "Existing straight physical positions are identical in both timing pools");
            near (choiceBeats (i, false), beats[i], "T off contains only original straight note lengths");
            near (syncMilliseconds (position, 123, false), beats[i] * 60000.0 / 123.0,
                  "T off preserves original note durations");
            require (std::wstring (syncLabel (position, false)) == labels[i],
                     "T off preserves original straight labels");
        }
        for (int i = 0; i <= 100; ++i)
        {
            const auto position = i / 100.0;
            require (syncIndex (position) == syncIndex (position, false) &&
                     syncMilliseconds (position, 123) == syncMilliseconds (position, 123, false),
                     "Omitting T retains the exact existing timing-helper behavior");
        }
        for (std::size_t requested = 1; requested < mixedBeats.size(); requested += 2)
        {
            const auto storedRequest = syncNormalized (requested, true);
            const auto restoredDuration = syncMilliseconds (storedRequest, 120, true);
            require (syncIndex (storedRequest, false) == (requested + 1) / 2,
                     "T off resolves an intermediate triplet request to the next straight control position");
            near (syncMilliseconds (storedRequest, 120, false), beats[(requested + 1) / 2] * 500.0,
                  "T off plays the resolved straight duration while retaining the raw triplet request");
            near (syncMilliseconds (storedRequest, 120, true), restoredDuration,
                  "T on restores the same retained triplet position without rewriting it");
        }
        require (syncNormalizedFromLabel (L"1/8T", false) < 0 &&
                 syncNormalizedFromLabel (L"1/16T", true) < 0,
                 "T off excludes triplet text and T on cannot request below the original shortest note");
        near (syncMilliseconds (0.0, 120, true), 125.0, "T preserves the original shortest musical duration");
        near (syncMilliseconds (1.0, 20, true), maximumCaptureMilliseconds,
              "T preserves the original longest duration and fixed capture capacity");
        near (syncNormalizedFromLabel (L"1/1", true), syncNormalized (8, true),
              "One-bar alias resolves correctly in the expanded pool");
        near (syncNormalizedFromLabel (L"2/1", true), 1.0,
              "Two-bar alias resolves correctly in the expanded pool");
        const auto tripletBufferMs = 1000.0 / 3.0;
        near (floorMilliseconds (tripletBufferMs, 120, false), 250.0,
              "A straight-only grain floors safely below a triplet-sized BUFFER");
        near (floorMilliseconds (tripletBufferMs, 120, true), tripletBufferMs,
              "A triplet-enabled grain can use the full triplet-sized BUFFER");
        near (quantizeMilliseconds (tripletBufferMs, 120, false), 250.0,
              "Straight-only duration quantization excludes the triplet choice");
        near (quantizeMilliseconds (tripletBufferMs, 120, true), tripletBufferMs,
              "Triplet-enabled duration quantization includes the triplet choice");
        require (syncIndex (-1, true) == 0 && syncIndex (2, true) == 10 &&
                 syncIndex (std::numeric_limits<double>::quiet_NaN(), true) == 0,
                 "Mixed sync positions remain bounded for hostile automation");
        std::cout << "Timing tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
