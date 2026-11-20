#include "GrainTiming.h"

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
        std::cout << "Timing tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
