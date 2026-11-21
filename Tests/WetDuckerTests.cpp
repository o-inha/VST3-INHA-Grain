#include "WetDucker.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int failures = 0;
void check (bool condition, const char* message)
{
    if (!condition) { std::fprintf (stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main()
{
    using InhaGrate::WetDucker;
    for (double rate : { 8000.0, 44100.0, 96000.0, 384000.0 })
    {
        WetDucker off, quiet, medium, loud, right;
        for (auto* d : { &off, &quiet, &medium, &loud, &right }) d->prepare (rate);
        float q = 0.0f, m = 0.0f, l = 0.0f, r = 0.0f;
        bool neutral = true, linked = true, finite = true;
        for (int n = 0; n < static_cast<int> (rate * 0.15); ++n)
        {
            neutral = neutral && off.process (0.8f, -0.3f, 0.0f) == 1.0f;
            q = quiet.process (0.005f, -0.004f, 100.0f);
            m = medium.process (0.08f, -0.04f, 100.0f);
            l = loud.process (0.5f, 0.0f, 100.0f);
            r = right.process (0.0f, -0.5f, 100.0f);
            linked = linked && l == r;
            finite = finite && std::isfinite (l) && l >= 0.0f && l <= 1.0f;
        }
        check (neutral, "Zero duck depth is exactly neutral");
        check (q == 1.0f && m > 0.05f && m < 0.9f && l < 0.001f,
               "Actual source level controls reduction; loud dry nearly mutes wet at100%");
        check (linked, "Either stereo input channel keys exactly the same linked gain");
        check (finite, "Fast attack remains bounded and finite at every sample rate");
        float recovered = 0.0f;
        for (int n = 0; n < static_cast<int> (rate * 0.8); ++n)
            recovered = loud.process (0.0f, 0.0f, 100.0f);
        check (recovered > 0.99f, "Wet recovers after dry input becomes silent");
        WetDucker half;
        half.prepare (rate);
        float h = 0.0f;
        for (int n = 0; n < static_cast<int> (rate * 0.1); ++n)
            h = half.process (0.6f, -0.4f, 50.0f);
        check (std::abs (h - 0.5f) < 0.001f, "50% depth retains half the wet amplitude on loud input");
        for (int n = 0; n < static_cast<int> (rate * 0.2); ++n)
            h = half.process (0.6f, -0.4f, 0.0f);
        check (h == 1.0f, "Returning to0% after ducking restores exact unity at every sample rate");
        half.reset();
        check (half.process (0.0f, 0.0f, 100.0f) == 1.0f, "Reset removes old detector and reduction history");
        for (int n = 0; n < 10000; ++n)
            h = half.process (n % 2 ? std::numeric_limits<float>::quiet_NaN() : 0.6f,
                              n % 3 ? -0.4f : std::numeric_limits<float>::infinity(),
                              n % 2 ? 0.0f : 100.0f);
        check (std::isfinite (h) && h >= 0.0f && h <= 1.0f, "Automation and nonfinite inputs remain safe");
    }
    if (failures) return 1;
    std::puts ("Wet ducker tests passed");
    return 0;
}
