// End-to-end checks of the plugin processor, without a DAW.

#include "plugin/Parameters.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "dsp/Presets.h"

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

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

void setChoice (PsyDuckAudioProcessor& p, const char* id, int index)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
}

void setValue (PsyDuckAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** Runs the processor with a constant 1.0 on the main input and returns the left output.
    kickAt / noteAt: sample positions of a sidechain burst or a MIDI note (-1 for none). */
std::vector<float> run (PsyDuckAudioProcessor& p, int totalSamples, int kickAt, int noteAt,
                        int noteNumber = 36, bool sineInput = false)
{
    std::vector<float> out;
    out.reserve ((size_t) totalSamples);

    juce::AudioBuffer<float> buffer (4, blockSize);

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        juce::MidiBuffer midi;

        for (int i = 0; i < blockSize; ++i)
        {
            const int n = start + i;
            const float in = sineInput ? std::sin (0.6545f * (float) n) : 1.0f;   // 5 kHz
            buffer.setSample (0, i, in);
            buffer.setSample (1, i, in);

            const bool inKick = kickAt >= 0 && n >= kickAt && n < kickAt + 2400;
            const float kick = inKick ? 0.9f * std::sin (0.0065f * (float) (n - kickAt) + 1.0f) : 0.0f;
            buffer.setSample (2, i, kick);
            buffer.setSample (3, i, kick);

            if (n == noteAt)
                midi.addEvent (juce::MidiMessage::noteOn (1, noteNumber, 1.0f), i);
        }

        p.processBlock (buffer, midi);

        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    return out;
}

float minOf (const std::vector<float>& v, int from, int to)
{
    float m = 10.0f;
    for (int i = from; i < to; ++i)
        m = std::min (m, v[(size_t) i]);
    return m;
}

float maxOf (const std::vector<float>& v, int from, int to)
{
    float m = -10.0f;
    for (int i = from; i < to; ++i)
        m = std::max (m, v[(size_t) i]);
    return m;
}

std::unique_ptr<PsyDuckAudioProcessor> make()
{
    auto p = std::make_unique<PsyDuckAudioProcessor>();
    p->enableAllBuses();
    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);
    return p;
}

void syncMode()
{
    auto p = make();
    check (p->getLatencySamples() == 240, "5 ms lookahead is reported as 240 samples of latency");

    // No transport: free-runs at 120 BPM, one cycle per 24000 samples.
    const auto out = run (*p, 48000, -1, -1);
    check (minOf (out, 1000, 48000) < 0.01f, "sync mode ducks to silence");
    check (maxOf (out, 1000, 48000) > 0.99f, "sync mode returns to full level");

    setValue (*p, psyduck::params::amount, 0.0f);
    const auto dry = run (*p, 48000, -1, -1);
    check (minOf (dry, 24000, 48000) > 0.99f, "amount at zero leaves the signal untouched");
}

void audioMode()
{
    auto p = make();
    setChoice (*p, psyduck::params::mode, psyduck::params::modeAudio);

    const int kickAt = 24000;
    const auto out = run (*p, 72000, kickAt, -1);

    check (minOf (out, 2000, kickAt) > 0.99f, "audio mode does not duck before a kick");
    check (out[(size_t) (kickAt + 240)] < 0.05f, "the duck is in place when the delayed kick arrives");
    check (minOf (out, kickAt, kickAt + 240) < 0.5f, "lookahead starts lowering the gain before the kick");

    float biggestStep = 0.0f;
    for (int i = kickAt - 100; i < kickAt + 2000; ++i)
        biggestStep = std::max (biggestStep, std::abs (out[(size_t) i] - out[(size_t) i - 1]));
    check (biggestStep < 0.05f, "audio trigger has no clicks");

    check (minOf (out, 60000, 72000) > 0.99f, "audio mode recovers after the kick");
}

void midiMode()
{
    auto p = make();
    setChoice (*p, psyduck::params::mode, psyduck::params::modeMidi);

    const int noteAt = 24100;
    const auto out = run (*p, 72000, 30000, noteAt);

    check (minOf (out, 2000, noteAt) > 0.99f, "midi mode does not duck before a note");
    check (out[(size_t) (noteAt + 240)] < 0.05f, "a note starts the duck");
    check (minOf (out, 60000, 72000) > 0.99f, "midi mode ignores the sidechain audio");
}

void noteFilter()
{
    auto p = make();
    setChoice (*p, psyduck::params::mode, psyduck::params::modeMidi);
    setValue (*p, psyduck::params::midiNote, 37.0f);   // listens to MIDI note 36 only

    const auto wrong = run (*p, 48000, -1, 24000, 40);
    check (minOf (wrong, 2000, 48000) > 0.99f, "other notes are ignored when a note is chosen");

    const auto right = run (*p, 48000, -1, 24000, 36);
    check (right[(size_t) (24000 + 240)] < 0.05f, "the chosen note triggers");
}

void multibandAndInvert()
{
    auto p = make();
    setValue (*p, psyduck::params::multiband, 1.0f);

    // Constant input is pure low end: ducked as before.
    const auto low = run (*p, 48000, -1, -1);
    check (minOf (low, 4000, 48000) < 0.02f, "multiband still ducks the low end");

    // A 5 kHz tone is above the crossover: left alone.
    const auto high = run (*p, 48000, -1, -1, 36, true);
    bool untouched = true;
    for (int start = 4800; start + 480 <= 48000; start += 480)
        untouched = untouched && maxOf (high, start, start + 480) > 0.95f;
    check (untouched, "multiband leaves the highs untouched");

    auto q = make();
    setValue (*q, psyduck::params::amount, 0.0f);
    setValue (*q, psyduck::params::invert, 1.0f);
    const auto flipped = run (*q, 9600, -1, -1);
    check (flipped[9000] < -0.99f, "invert flips the polarity");
}

void followerMode()
{
    auto p = make();
    setChoice (*p, psyduck::params::mode, psyduck::params::modeFollower);

    const int kickAt = 24000;
    const auto out = run (*p, 96000, kickAt, -1);

    check (minOf (out, 2000, kickAt) > 0.99f, "follower does not duck without a kick");
    check (minOf (out, kickAt, kickAt + 2400) < 0.02f, "follower ducks while the kick plays");
    check (minOf (out, 80000, 96000) > 0.99f, "follower recovers after the kick");
}

void threeBands()
{
    // Low fully ducked, highs at half: a 5 kHz tone must dip to about half.
    auto p = make();
    setValue (*p, psyduck::params::multiband, 1.0f);
    setValue (*p, psyduck::params::amountHigh, 50.0f);

    const auto high = run (*p, 48000, -1, -1, 36, true);
    float lowestPeak = 10.0f, highestPeak = 0.0f;
    for (int start = 4800; start + 480 <= 48000; start += 480)
    {
        const float peak = maxOf (high, start, start + 480);
        lowestPeak = std::min (lowestPeak, peak);
        highestPeak = std::max (highestPeak, peak);
    }
    check (lowestPeak > 0.42f && lowestPeak < 0.58f, "high band ducks by its own amount");
    check (highestPeak > 0.95f, "high band returns to full level");

    // Each band follows its own curve: a flat high curve means no ducking there.
    p->setCurve (psyduck::makePreset (5), 2);
    setValue (*p, psyduck::params::amountHigh, 100.0f);
    const auto flat = run (*p, 48000, -1, -1, 36, true);
    bool untouched = true;
    for (int start = 9600; start + 480 <= 48000; start += 480)
        untouched = untouched && maxOf (flat, start, start + 480) > 0.95f;
    check (untouched, "each band uses its own curve");
}

void filterAndPan()
{
    auto f = make();
    setChoice (*f, psyduck::params::target, psyduck::params::targetFilter);
    const auto filtered = run (*f, 48000, -1, -1, 36, true);
    float lowestPeak = 10.0f, highestPeak = 0.0f;
    for (int start = 4800; start + 480 <= 48000; start += 480)
    {
        const float peak = maxOf (filtered, start, start + 480);
        lowestPeak = std::min (lowestPeak, peak);
        highestPeak = std::max (highestPeak, peak);
    }
    check (lowestPeak < 0.02f, "filter target closes on the highs at the curve bottom");
    check (highestPeak > 0.8f, "filter target opens at the curve top");

    auto q = make();
    setChoice (*q, psyduck::params::target, psyduck::params::targetPan);
    const auto left = run (*q, 48000, -1, -1);
    check (minOf (left, 4800, 48000) < 0.05f, "pan target moves the sound away from the left");
    check (maxOf (left, 4800, 48000) > 0.99f, "pan target keeps full level on the near side");
    check (minOf (left, 4800, 48000) >= 0.0f, "pan target never inverts a channel");
}

void longPattern()
{
    auto p = make();
    setChoice (*p, psyduck::params::rate, 10);   // 4 bars = 8 s at 120 BPM
    const auto out = run (*p, 96000, -1, -1);
    check (minOf (out, 1000, 20000) < 0.01f, "a 4 bar cycle still starts ducked");
    check (maxOf (out, 60000, 96000) < 0.9f, "a 4 bar cycle moves slowly");
}

void alignment()
{
    auto p = make();
    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::MidiBuffer midi;
    const int shift = 96;   // the bass is 2 ms early

    const auto burst = [] (int n)
    {
        if (n < 0)
            return 0.0f;
        const double local = std::fmod ((double) n / sampleRate, 0.25);
        return (float) (std::exp (-local * 14.0) * std::sin (2.0 * 3.14159265358979 * 55.0 * local));
    };

    for (int start = 0; start < 144000; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float bass = -burst (start + i + shift);   // early and inverted
            const float kick = burst (start + i);
            buffer.setSample (0, i, bass);
            buffer.setSample (1, i, bass);
            buffer.setSample (2, i, kick);
            buffer.setSample (3, i, kick);
        }
        p->processBlock (buffer, midi);
    }

    float lagMs = 0.0f;
    const auto result = p->findAlignment (lagMs);
    check (result.valid, "alignment finds kick and bass");
    check (std::abs (lagMs - 2.0f) < 0.2f, "alignment measures how early the bass is");
    check (result.invert, "alignment notices the inverted bass");
    check (result.after > 0.95f, "alignment predicts a full low end");
}

void slots()
{
    auto p = make();
    setValue (*p, psyduck::params::amount, 30.0f);
    p->setCurve (psyduck::makePreset (5));

    p->switchSlot();   // B starts as a copy of A
    check (p->getActiveSlot() == 1, "switching goes to slot B");
    setValue (*p, psyduck::params::amount, 80.0f);
    p->setCurve (psyduck::makePreset (1));

    p->switchSlot();
    check (std::abs (p->apvts.getRawParameterValue (psyduck::params::amount)->load() - 30.0f) < 0.2f, "slot A keeps its knobs");
    check (p->getCurve().size() == 2, "slot A keeps its curve");

    p->switchSlot();
    check (std::abs (p->apvts.getRawParameterValue (psyduck::params::amount)->load() - 80.0f) < 0.2f, "slot B keeps its knobs");
    check (p->getCurve().size() == 5, "slot B keeps its curve");

    // Both slots survive saving and loading the project.
    juce::MemoryBlock data;
    p->getStateInformation (data);
    auto q = make();
    q->setStateInformation (data.getData(), (int) data.getSize());
    check (q->getActiveSlot() == 1 && q->getCurve().size() == 5, "the active slot is restored");
    q->switchSlot();
    check (q->getCurve().size() == 2, "the other slot is restored too");

    // Saving many times must not make the saved data grow.
    juce::MemoryBlock again;
    q->switchSlot();
    q->getStateInformation (again);
    auto r = make();
    r->setStateInformation (again.getData(), (int) again.getSize());
    juce::MemoryBlock third;
    r->getStateInformation (third);
    check (third.getSize() < again.getSize() + 64, "saved state does not grow with each save");
}

void state()
{
    auto a = make();
    a->setCurve (psyduck::makePreset (5));
    setChoice (*a, psyduck::params::mode, psyduck::params::modeAudio);

    juce::MemoryBlock data;
    a->getStateInformation (data);

    auto b = make();
    b->setStateInformation (data.getData(), (int) data.getSize());

    check (b->getCurve().size() == 2, "the curve is saved and restored");

    a->setCurve (psyduck::makePreset (5), 1);
    a->gridColumns.store (32);
    juce::MemoryBlock more;
    a->getStateInformation (more);
    b->setStateInformation (more.getData(), (int) more.getSize());
    check (b->getCurve (1).size() == 2 && b->getCurve (2).size() == 5, "band curves are saved and restored");
    check (b->gridColumns.load() == 32, "the grid setting is saved and restored");
    check (juce::roundToInt (b->apvts.getRawParameterValue (psyduck::params::mode)->load()) == psyduck::params::modeAudio,
           "parameters are saved and restored");
}
} // namespace

/** Renders the editor to a PNG so the layout can be checked without a DAW. */
int screenshot (const juce::String& path, int mode, const juce::String& options)
{
    auto p = make();
    setChoice (*p, psyduck::params::mode, mode);
    if (options.contains ("multiband")) { setValue (*p, psyduck::params::multiband, 1.0f); p->editBand.store (1); p->setCurve (psyduck::makePreset (2), 1); }
    if (options.contains ("filter")) setChoice (*p, psyduck::params::target, psyduck::params::targetFilter);
    if (options.contains ("pan")) setChoice (*p, psyduck::params::target, psyduck::params::targetPan);
    run (*p, 48000, 0, -1);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    if (options.contains ("wave"))
        if (auto* e = dynamic_cast<PsyDuckAudioProcessorEditor*> (editor.get()))
            for (auto* child : e->getChildren())
                if (auto* curve = dynamic_cast<CurveEditor*> (child))
                    curve->showWaveform = true;

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    juce::File file (path);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream) ? 0 : 1;
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juce;

    if (argc >= 3 && juce::String (argv[1]) == "--screenshot")
        return screenshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).getFullPathName(),
                           argc >= 4 ? juce::String (argv[3]).getIntValue() : 0,
                           argc >= 5 ? juce::String (argv[4]) : juce::String());

    syncMode();
    audioMode();
    midiMode();
    noteFilter();
    multibandAndInvert();
    followerMode();
    threeBands();
    filterAndPan();
    longPattern();
    alignment();
    slots();
    state();

    if (failures == 0)
        std::printf ("All plugin tests passed.\n");

    return failures == 0 ? 0 : 1;
}
