#include "dsp/Crossover.h"
#include "dsp/Curve.h"
#include "dsp/CurveSuggest.h"
#include "dsp/DelayLine.h"
#include "dsp/EnvelopeFollower.h"
#include "dsp/GainProcessor.h"
#include "dsp/OneShot.h"
#include "dsp/PhaseMeter.h"
#include "dsp/Presets.h"
#include "dsp/SyncEngine.h"
#include "dsp/TriggerDetector.h"

#include <cmath>
#include <cstdio>
#include <vector>

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

    check (near (c.evaluate (1.0f), 0.0), "default curve falls before the cycle restarts");

    float previous = 0.0f;
    bool monotonic = true, inRange = true;
    for (int i = 0; i <= 950; ++i)
    {
        const float v = c.evaluate ((float) i / 1000.0f);
        monotonic = monotonic && v >= previous - 1.0e-6f;
        inRange = inRange && v >= 0.0f && v <= 1.0f;
        previous = v;
    }
    check (monotonic, "default curve only rises until the final fall");
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
        added = added && full.addPoint ({ (float) i / 32.0f, 1.0f, 0.0f }) >= 0;
    check (added, "curve accepts maxPoints points");
    check (full.addPoint ({ 0.5f, 0.5f, 0.0f }) == -1, "curve rejects points beyond its capacity");
}

void editTests()
{
    psyduck::Curve c;
    c.clear();
    c.addPoint ({ 0.0f, 0.0f, 0.0f });
    c.addPoint ({ 1.0f, 1.0f, 0.0f });

    const int index = c.addPoint ({ 0.5f, 0.2f, 0.0f });
    check (index == 1 && c.size() == 3, "new point lands between its neighbours");

    c.movePoint (1, 2.0f, 0.9f);
    check (near (c[1].x, 1.0) && near (c[1].y, 0.9), "a point cannot pass the next point");

    c.movePoint (0, 0.4f, 0.5f);
    check (near (c[0].x, 0.0) && near (c[0].y, 0.5), "first point only moves up and down");
    c.movePoint (2, 0.4f, 0.5f);
    check (near (c[2].x, 1.0), "last point only moves up and down");

    check (! c.removePoint (0) && ! c.removePoint (2), "end points cannot be removed");
    check (c.removePoint (1) && c.size() == 2, "inner points can be removed");

    c.movePoint (0, 0.0f, 0.0f);
    c.movePoint (1, 1.0f, 1.0f);
    c.setSegmentCurve (0, 1.0f);
    check (c.evaluate (0.5f) > 0.8f, "positive bend rises fast");
    c.setSegmentCurve (0, -1.0f);
    check (c.evaluate (0.5f) < 0.2f, "negative bend rises late");

    for (int i = 0; i < psyduck::numPresets; ++i)
    {
        const auto preset = psyduck::makePreset (i);
        bool ok = preset.size() >= 2 && near (preset[0].x, 0.0) && near (preset[preset.size() - 1].x, 1.0);
        check (ok, "every preset spans the whole cycle");
    }
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
void triggerTests()
{
    const double sr = 48000.0;
    const float threshold = 0.25f;
    const int retrigger = (int) (0.080 * sr);

    psyduck::TriggerDetector d;
    d.prepare (sr);

    // Four decaying 50 Hz "kicks", 0.4 s apart, must give exactly four triggers.
    int triggers = 0;
    int firstTrigger = -1;
    const int kickSpacing = (int) (0.4 * sr);
    for (int n = 0; n < kickSpacing * 4; ++n)
    {
        const int t = n % kickSpacing;
        const double env = std::exp (-(double) t / (0.12 * sr));
        const float x = (float) (env * std::sin (2.0 * 3.14159265358979 * 50.0 * (double) t / sr + 1.2));
        if (d.process (std::abs (x), threshold, retrigger))
        {
            ++triggers;
            if (firstTrigger < 0)
                firstTrigger = n;
        }
    }
    check (triggers == 4, "each kick triggers exactly once");
    check (firstTrigger >= 0 && firstTrigger < 48, "the trigger fires within a millisecond of the hit");

    psyduck::TriggerDetector quiet;
    quiet.prepare (sr);
    bool fired = false;
    for (int n = 0; n < 48000; ++n)
        fired = fired || quiet.process (0.1f, threshold, retrigger);
    check (! fired, "signal below the threshold never triggers");
}

void oneShotTests()
{
    psyduck::Curve c;
    psyduck::OneShot shot;
    shot.reset();
    shot.setLength (1000.0);

    check (near (shot.next (c), 1.0), "idle one-shot does not duck");
    check (shot.getPhase() < 0.0f, "idle one-shot reports no position");

    // Without lookahead the curve starts on the trigger sample.
    shot.trigger (0);
    check (near (shot.next (c), c.evaluate (0.0f)), "trigger starts the curve immediately");
    for (int i = 0; i < 499; ++i)
        shot.next (c);
    check (near (shot.getPhase(), 0.5, 1.0e-3), "one-shot follows the cycle length");
    for (int i = 0; i < 600; ++i)
        shot.next (c);
    check (! shot.isActive() && near (shot.next (c), 1.0), "one-shot returns to unity when the curve ends");

    // With lookahead the gain glides down and the curve starts exactly on time.
    shot.reset();
    shot.trigger (100);
    float previous = 1.0f, biggestStep = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        const float v = shot.next (c);
        biggestStep = std::max (biggestStep, std::abs (v - previous));
        previous = v;
        check (! shot.isActive(), "curve has not started during the lookahead");
    }
    check (biggestStep < 0.03f, "lookahead glide has no jumps");
    check (near (previous, c.evaluate (0.0f), 0.02), "glide ends at the start of the curve");
    shot.next (c);
    check (shot.isActive(), "curve starts right after the lookahead");
}

void delayTests()
{
    psyduck::DelayLine d;
    d.prepare (64);

    bool ok = true;
    for (int n = 0; n < 500; ++n)
    {
        const float out = d.process ((float) n, 10);
        ok = ok && near (out, n < 10 ? 0.0 : (double) (n - 10));
    }
    check (ok, "delay line delays by the requested number of samples");

    psyduck::DelayLine zero;
    zero.prepare (64);
    check (near (zero.process (0.7f, 0), 0.7), "zero delay passes the signal through");
}
double sineRms (double frequency, bool wantLow, bool wantSum)
{
    const double sr = 48000.0;
    psyduck::BandSplit split;
    split.setFrequency (150.0, sr);

    double sum = 0.0;
    int count = 0;
    for (int n = 0; n < 48000; ++n)
    {
        const float x = (float) std::sin (2.0 * 3.14159265358979 * frequency * n / sr);
        float low = 0.0f, high = 0.0f;
        split.process (x, low, high);
        const double v = wantSum ? (double) low + high : (wantLow ? low : high);
        if (n > 24000)
        {
            sum += v * v;
            ++count;
        }
    }
    return std::sqrt (sum / count);
}

void crossoverTests()
{
    const double full = std::sqrt (0.5);
    check (near (sineRms (40.0, true, false), full, 0.03), "low band keeps the sub");
    check (sineRms (2000.0, true, false) < 0.001, "low band removes the highs");
    check (near (sineRms (2000.0, false, false), full, 0.03), "high band keeps the highs");
    check (sineRms (30.0, false, false) < 0.02, "high band removes the sub");

    for (double f : { 40.0, 100.0, 150.0, 300.0, 1000.0, 8000.0 })
        check (near (sineRms (f, false, true), full, 0.02), "bands add back up to the full level");
}

float correlationOf (double phaseShift)
{
    const double sr = 48000.0;
    psyduck::PhaseMeter meter;
    meter.prepare (sr);
    for (int n = 0; n < 48000; ++n)
    {
        const double t = 2.0 * 3.14159265358979 * 55.0 * n / sr;
        meter.process ((float) std::sin (t), (float) std::sin (t + phaseShift));
    }
    check (meter.hasSignal(), "phase meter sees the signal");
    return meter.getCorrelation();
}

void phaseTests()
{
    check (correlationOf (0.0) > 0.98f, "identical low end reads as in phase");
    check (correlationOf (3.14159265358979) < -0.98f, "inverted low end reads as cancelling");
    check (std::abs (correlationOf (1.5707963)) < 0.1f, "a quarter-cycle shift reads as neutral");

    psyduck::PhaseMeter silent;
    silent.prepare (48000.0);
    for (int n = 0; n < 1000; ++n)
        silent.process (0.0f, 0.5f);
    check (! silent.hasSignal(), "phase meter reports nothing without a kick");
}

void followerTests()
{
    const double sr = 48000.0;
    psyduck::EnvelopeFollower f;
    f.prepare (sr);
    f.setRelease (100.0f);

    check (near (f.process (0.0f, 0.5f), 1.0), "follower does not duck in silence");

    float v = 1.0f;
    for (int n = 0; n < 480; ++n)
        v = f.process (0.8f, 0.5f);
    check (v < 0.01f, "a loud kick ducks fully");

    for (int n = 0; n < 4800; ++n)
        v = f.process (0.0f, 0.5f);
    check (v > 0.3f && v < 0.6f, "the duck eases off with the release time");

    for (int n = 0; n < 48000; ++n)
        v = f.process (0.0f, 0.5f);
    check (v > 0.99f, "follower returns to full level");

    psyduck::EnvelopeFollower half;
    half.prepare (sr);
    for (int n = 0; n < 4800; ++n)
        v = half.process (0.25f, 0.5f);
    check (near (v, 0.5, 0.02), "a quieter kick ducks less");
}

void suggestTests()
{
    psyduck::Curve c;

    std::vector<float> silence (256, 0.0f);
    check (! psyduck::suggestCurve (silence, c), "no suggestion without a kick");

    // Kick at the start of the cycle, fading over roughly a third of it.
    std::vector<float> kick (256, 0.0f);
    for (int i = 0; i < 256; ++i)
        kick[(size_t) i] = 0.9f * (float) std::exp (-(double) i / 25.0) * (i % 3 == 0 ? 1.0f : 0.6f);

    check (psyduck::suggestCurve (kick, c), "a kick gives a suggestion");
    check (near (c.evaluate (0.0f), 0.0) && near (c.evaluate (0.03f), 0.0), "suggestion is silent over the kick body");
    check (c.evaluate (0.2f) > 0.0f && c.evaluate (0.2f) < 1.0f, "suggestion rises while the tail fades");
    check (near (c.evaluate (0.6f), 1.0), "suggestion is fully open after the kick");

    // Kick in the middle of the cycle.
    std::vector<float> late (256, 0.0f);
    for (int i = 128; i < 256; ++i)
        late[(size_t) i] = 0.9f * (float) std::exp (-(double) (i - 128) / 15.0);

    check (psyduck::suggestCurve (late, c), "an off-grid kick gives a suggestion");
    check (near (c.evaluate (0.2f), 1.0), "suggestion stays open before a late kick");
    check (near (c.evaluate (0.51f), 0.0), "suggestion ducks where the late kick is");
}
} // namespace

int main()
{
    curveTests();
    editTests();
    syncTests();
    gainTests();
    triggerTests();
    oneShotTests();
    delayTests();
    crossoverTests();
    phaseTests();
    followerTests();
    suggestTests();

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");

    return failures == 0 ? 0 : 1;
}
