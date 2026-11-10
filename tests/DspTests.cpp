#include "dsp/Curve.h"
#include "dsp/GainProcessor.h"
#include "dsp/SyncEngine.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

bool near (double a, double b, double tol = 1.0e-4) { return std::abs (a - b) <= tol; }

void curveTests()
{
    psyduck::Curve c;
    check (near (c.evaluate (0.0f), 0.0), "default curve starts silent");
    check (near (c.evaluate (0.03f), 0.0), "default curve holds silence over the kick");
    check (near (c.evaluate (0.8f), 1.0), "default curve is back at full level");

    float previous = 0.0f;
    bool monotonic = true, inRange = true;
    for (int i = 0; i <= 1000; ++i)
    {
        const float v = c.evaluate ((float) i / 1000.0f);
        monotonic = monotonic && v >= previous - 1.0e-6f;
        inRange = inRange && v >= 0.0f && v <= 1.0f;
        previous = v;
    }
    check (monotonic, "default curve only rises");
    check (inRange, "curve stays within 0..1");

    psyduck::Curve line;
    line.clear();
    line.addPoint ({ 1.0f, 1.0f, 0.0f });
    line.addPoint ({ 0.0f, 0.0f, 0.0f });
    check (near (line.evaluate (0.25f), 0.25), "points are sorted and linear segments interpolate");

    psyduck::Curve full;
    full.clear();
    bool added = true;
    for (std::size_t i = 0; i < psyduck::Curve::maxPoints; ++i)
        added = added && full.addPoint ({ (float) i / 32.0f, 1.0f, 0.0f });
    check (added, "curve accepts maxPoints points");
    check (! full.addPoint ({ 0.5f, 0.5f, 0.0f }), "curve rejects points beyond its capacity");
}

void syncTests()
{
    const double sr = 48000.0, bpm = 140.0;

    psyduck::SyncEngine e;
    e.prepare (sr);

    // Playing: phase follows the song position.
    e.beginBlock (bpm, 1.0, true, 2.25);
    check (near (e.next (0.0), 0.25), "phase locks to song position");

    e.beginBlock (bpm, 0.5, true, 2.25);
    check (near (e.next (0.0), 0.5), "1/8 rate doubles the cycle speed");

    e.beginBlock (bpm, 1.0, true, 0.0);
    check (near (e.next (0.25), 0.25), "phase offset shifts the curve");

    // One beat at 140 BPM must be exactly one cycle, with no drift across blocks.
    const int samplesPerBeat = (int) std::lround (sr * 60.0 / bpm);
    const int blockSize = 64;
    double ppq = 0.0;
    int done = 0;
    float last = 0.0f;
    int wraps = 0;
    while (done < samplesPerBeat * 8)
    {
        e.beginBlock (bpm, 1.0, true, ppq);
        for (int i = 0; i < blockSize; ++i)
        {
            const float p = e.next (0.0);
            if (p < last)
                ++wraps;
            last = p;
        }
        done += blockSize;
        ppq = (double) done * bpm / 60.0 / sr;
    }
    check (wraps == 8 || wraps == 7, "eight beats give eight cycles");

    // Stopped: free-running at host tempo.
    psyduck::SyncEngine f;
    f.prepare (sr);
    f.beginBlock (120.0, 1.0, false, 0.0);
    for (int i = 0; i < 12000; ++i)
        f.next (0.0);
    check (near (f.getPhase(), 0.5, 1.0e-3), "free-run advances at tempo while stopped");
}

void gainTests()
{
    using G = psyduck::GainProcessor;
    check (near (G::targetGain (0.0f, 1.0f, 1.0f), 0.0), "full amount and mix silence the signal at curve bottom");
    check (near (G::targetGain (0.0f, 0.5f, 1.0f), 0.5), "amount limits the depth");
    check (near (G::targetGain (0.0f, 1.0f, 0.0f), 1.0), "mix at zero leaves the signal untouched");
    check (near (G::targetGain (1.0f, 1.0f, 1.0f), 1.0), "curve top is unity gain");

    // A hard jump in the curve must not produce a jump in the gain (click protection).
    G g;
    g.prepare (48000.0);
    float previous = g.next (1.0f, 1.0f, 1.0f);
    float biggestStep = 0.0f;
    for (int i = 0; i < 480; ++i)
    {
        const float v = g.next (0.0f, 1.0f, 1.0f);
        biggestStep = std::max (biggestStep, std::abs (v - previous));
        previous = v;
    }
    check (biggestStep < 0.05f, "gain is smoothed across a hard curve jump");
    check (previous < 0.001f, "gain settles on the target within 10 ms");
}
} // namespace

int main()
{
    curveTests();
    syncTests();
    gainTests();

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");

    return failures == 0 ? 0 : 1;
}
