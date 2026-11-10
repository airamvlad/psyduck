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
    check (juce::roundToInt (b->apvts.getRawParameterValue (psyduck::params::mode)->load()) == psyduck::params::modeAudio,
           "parameters are saved and restored");
}
} // namespace

/** Renders the editor to a PNG so the layout can be checked without a DAW. */
int screenshot (const juce::String& path, int mode)
{
    auto p = make();
    setChoice (*p, psyduck::params::mode, mode);
    run (*p, 48000, 0, -1);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());

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
                           argc >= 4 ? juce::String (argv[3]).getIntValue() : 0);

    syncMode();
    audioMode();
    midiMode();
    noteFilter();
    multibandAndInvert();
    followerMode();
    state();

    if (failures == 0)
        std::printf ("All plugin tests passed.\n");

    return failures == 0 ? 0 : 1;
}
