#include "BlackThrumPlugin.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr int numChannels = 2;
constexpr int blockSamples = 4096;
constexpr std::array<const char*, 7> expectedIDs {
    "pitch_offset",
    "thrum",
    "drift",
    "formant",
    "width",
    "grind",
    "output"
};
constexpr std::array<const char*, 7> expectedNames {
    "Pitch offset",
    "Thrum",
    "Drift",
    "Formant",
    "Width",
    "Grind",
    "Output"
};
constexpr std::array<float, 7> validStateValues {
    0.0f,
    0.68f,
    0.42f,
    0.52f,
    0.44f,
    0.38f,
    0.72f
};

class PluginHarness
{
public:
    PluginHarness()
        : audio (numChannels, blockSamples)
        , context { audio, midi, automation, nullptr, {}, {} }
    {
        plugin.prepareToPlay (yup::AudioSpec (48000.0f, blockSamples, numChannels));
    }

    float process()
    {
        audio.clear();
        plugin.processBlock (context);
        return peak();
    }

    std::array<float, blockSamples> processLeft()
    {
        audio.clear();
        plugin.processBlock (context);

        std::array<float, blockSamples> result {};
        const auto* samples = audio.getReadPointer (0);
        for (int sample = 0; sample < blockSamples; ++sample)
            result[static_cast<std::size_t> (sample)] = samples[sample];
        return result;
    }

    void addMidiNoteOn (int note, int sample)
    {
        midi.addEvent (yup::MidiMessage::noteOn (1, note, 1.0f), sample);
    }

    void addMidiNoteOff (int note, int sample)
    {
        midi.addEvent (yup::MidiMessage::noteOff (1, note), sample);
    }

    float peak() const
    {
        float result = 0.0f;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                result = std::max (result, std::fabs (samples[sample]));
        }
        return result;
    }

    blackthrum::plugin::BlackThrumPlugin plugin;

private:
    yup::AudioBuffer<float> audio;
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer automation;
    yup::AudioProcessContext<float> context;
};

struct StateRecord
{
    yup::String id;
    float value = 0.0f;
};

yup::MemoryBlock makeState (int version, int presetIndex, int parameterCount, const std::vector<StateRecord>& records)
{
    yup::MemoryBlock state;
    yup::MemoryOutputStream stream (state, false);
    constexpr std::array<char, 4> magic {{ 'B', 'L', 'T', '1' }};

    assert (stream.write (magic.data(), magic.size()));
    assert (stream.writeInt (version));
    assert (stream.writeInt (presetIndex));
    assert (stream.writeInt (parameterCount));
    for (const auto& record : records)
    {
        assert (stream.writeString (record.id));
        assert (stream.writeFloat (record.value));
    }
    stream.flush();
    return state;
}

std::vector<StateRecord> makeValidRecords()
{
    std::vector<StateRecord> records;
    records.reserve (expectedIDs.size());
    for (std::size_t i = 0; i < expectedIDs.size(); ++i)
        records.push_back ({ expectedIDs[i], validStateValues[i] });
    return records;
}

void expectLoadFailure (const yup::MemoryBlock& state)
{
    PluginHarness target;
    const auto before = target.plugin.getParameters()[1]->getValue();
    assert (target.plugin.loadStateFromMemory (state).failed());
    assert (target.plugin.getParameters()[1]->getValue() == before);
}

void processUntilSilent (PluginHarness& harness)
{
    for (int i = 0; i < 80; ++i)
    {
        const auto peak = harness.process();
        if (peak <= 1.0e-5f)
            return;
    }

    assert (false);
}

void testParameterSurfaceIsStableAndTriggerIsRuntimeOnly()
{
    PluginHarness harness;
    const auto parameters = harness.plugin.getParameters();

    assert (parameters.size() == expectedIDs.size());
    for (std::size_t i = 0; i < expectedIDs.size(); ++i)
    {
        assert (parameters[i]->getID() == expectedIDs[i]);
        assert (parameters[i]->getName() == expectedNames[i]);
    }
    assert (harness.plugin.getParameterByID ("trigger") == nullptr);
}

void testStateRoundTripAndMagicRejection()
{
    PluginHarness source;
    source.plugin.setCurrentPreset (2);
    auto parameters = source.plugin.getParameters();
    parameters[0]->setValue (-7.0f);
    parameters[1]->setValue (0.9f);
    parameters[2]->setValue (0.8f);
    source.plugin.setStandaloneTriggerGate (true);

    yup::MemoryBlock state;
    assert (source.plugin.saveStateIntoMemory (state).wasOk());

    PluginHarness target;
    assert (target.plugin.loadStateFromMemory (state).wasOk());
    assert (target.plugin.getCurrentPreset() == 2);
    assert (! target.plugin.isStandaloneTriggerGateRequested());
    assert (std::fabs (target.plugin.getParameters()[0]->getValue() + 7.0f) < 1.0e-6f);
    assert (std::fabs (target.plugin.getParameters()[1]->getValue() - 0.9f) < 1.0e-6f);
    assert (std::fabs (target.plugin.getParameters()[2]->getValue() - 0.8f) < 1.0e-6f);

    auto* bytes = static_cast<char*> (state.getData());
    bytes[0] = 'X';
    PluginHarness rejected;
    assert (rejected.plugin.loadStateFromMemory (state).failed());
}

void testStateRejectsMalformedRecords()
{
    auto validRecords = makeValidRecords();

    auto trailing = makeState (1, 0, static_cast<int> (validRecords.size()), validRecords);
    constexpr char extra = 0x2a;
    trailing.append (&extra, 1);
    expectLoadFailure (trailing);

    expectLoadFailure (makeState (2, 0, static_cast<int> (validRecords.size()), validRecords));
    expectLoadFailure (makeState (1, 0, static_cast<int> (validRecords.size()) - 1, validRecords));
    expectLoadFailure (makeState (1, 0, static_cast<int> (validRecords.size()) + 1, validRecords));

    auto duplicate = validRecords;
    duplicate[3].id = duplicate[2].id;
    expectLoadFailure (makeState (1, 0, static_cast<int> (duplicate.size()), duplicate));

    auto unknown = validRecords;
    unknown[2].id = "unknown_parameter";
    expectLoadFailure (makeState (1, 0, static_cast<int> (unknown.size()), unknown));

    auto outOfRange = validRecords;
    outOfRange[1].value = 2.0f;
    expectLoadFailure (makeState (1, 0, static_cast<int> (outOfRange.size()), outOfRange));

    auto nonFinite = validRecords;
    nonFinite[1].value = std::numeric_limits<float>::quiet_NaN();
    expectLoadFailure (makeState (1, 0, static_cast<int> (nonFinite.size()), nonFinite));
}

void testFreshPluginWithoutTriggersStaysSilentAndMeterOff()
{
    PluginHarness harness;

    const auto peak = harness.process();

    assert (peak <= 1.0e-7f);
    assert (harness.plugin.getOutputPeakLevel() <= 1.0e-7f);
}

void testProcessBlockRealtimeForbiddenPatternGuard()
{
    std::ifstream input (std::string (BLACKTHRUM_SOURCE_DIR) + "/source/BlackThrumPlugin.cpp");
    assert (input.good());
    const std::string source ((std::istreambuf_iterator<char> (input)), std::istreambuf_iterator<char>());
    const auto signature = source.find ("void BlackThrumPlugin::processBlock");
    assert (signature != std::string::npos);
    const auto bodyStart = source.find ('{', signature);
    assert (bodyStart != std::string::npos);

    int depth = 0;
    std::size_t bodyEnd = std::string::npos;
    for (std::size_t i = bodyStart; i < source.size(); ++i)
    {
        if (source[i] == '{')
            ++depth;
        else if (source[i] == '}')
        {
            --depth;
            if (depth == 0)
            {
                bodyEnd = i;
                break;
            }
        }
    }
    assert (bodyEnd != std::string::npos);

    const auto body = source.substr (bodyStart, bodyEnd - bodyStart + 1);
    constexpr std::array<const char*, 18> forbiddenPatterns {
        "new ",
        "delete ",
        "malloc",
        "free(",
        "std::lock",
        "mutex",
        "CriticalSection",
        "ScopedLock",
        "std::cout",
        "std::cerr",
        "printf",
        "Logger",
        "File",
        "URL",
        "socket",
        "http",
        "repaint",
        "sendChange"
    };

    for (const auto* pattern : forbiddenPatterns)
        assert (body.find (pattern) == std::string::npos);
}

void testHeldSyntheticTriggerRendersAndMeters()
{
    PluginHarness harness;
    harness.plugin.setStandaloneTriggerGate (true);

    const auto peak = harness.process();

    assert (harness.plugin.isStandaloneTriggerGateRequested());
    assert (harness.plugin.getOutputPeakLevel() > 0.0f);
    assert (peak > 1.0e-5f);
    assert (peak <= 0.98f);
}

void testRapidOnOffBeforeCallbackStillRendersRelease()
{
    PluginHarness harness;

    const auto startEdges = harness.plugin.getStandaloneTriggerEdgeCountForTests();
    harness.plugin.setStandaloneTriggerGate (true);
    harness.plugin.setStandaloneTriggerGate (false);
    assert (harness.plugin.getStandaloneTriggerEdgeCountForTests() == startEdges + 2u);
    assert (! harness.plugin.isStandaloneTriggerGateRequested());

    const auto peak = harness.process();
    assert (peak > 1.0e-5f);

    processUntilSilent (harness);
}

void testMidiNoteOffRestartsHeldStandaloneGate()
{
    PluginHarness harness;

    harness.plugin.setStandaloneTriggerGate (true);
    assert (harness.process() > 1.0e-5f);

    harness.addMidiNoteOn (60, 0);
    assert (harness.process() > 1.0e-5f);

    harness.addMidiNoteOff (60, 0);
    assert (harness.process() > 1.0e-5f);

    harness.plugin.setStandaloneTriggerGate (false);
    processUntilSilent (harness);
}

void testUiPressReleaseDoesNotInterruptHeldMidi()
{
    PluginHarness midiOnly;
    midiOnly.addMidiNoteOn (60, 0);
    const auto expected = midiOnly.processLeft();

    PluginHarness withUiEdges;
    withUiEdges.addMidiNoteOn (60, 0);
    withUiEdges.plugin.setStandaloneTriggerGate (true);
    withUiEdges.plugin.setStandaloneTriggerGate (false);
    const auto actual = withUiEdges.processLeft();

    assert (actual == expected);

    withUiEdges.addMidiNoteOff (60, 0);
    withUiEdges.process();
    processUntilSilent (withUiEdges);
}

void testHeldUiGateDoesNotInterruptMidiUntilMidiOff()
{
    PluginHarness midiOnly;
    midiOnly.addMidiNoteOn (60, 0);
    const auto expected = midiOnly.processLeft();

    PluginHarness withHeldUiGate;
    withHeldUiGate.addMidiNoteOn (60, 0);
    withHeldUiGate.plugin.setStandaloneTriggerGate (true);
    const auto actual = withHeldUiGate.processLeft();

    assert (actual == expected);
    assert (withHeldUiGate.plugin.isStandaloneTriggerGateRequested());

    withHeldUiGate.addMidiNoteOff (60, 0);
    assert (withHeldUiGate.process() > 1.0e-5f);

    withHeldUiGate.plugin.setStandaloneTriggerGate (false);
    processUntilSilent (withHeldUiGate);
}

void testFlushDoesNotSuppressHeldStandaloneGate()
{
    PluginHarness harness;

    harness.plugin.setStandaloneTriggerGate (true);
    assert (harness.process() > 1.0e-5f);

    harness.plugin.flush();
    assert (harness.plugin.isStandaloneTriggerGateRequested());
    assert (harness.process() > 1.0e-5f);

    harness.plugin.setStandaloneTriggerGate (false);
    processUntilSilent (harness);
}

void testReleaseGateDecaysToSilence()
{
    PluginHarness harness;

    harness.plugin.setStandaloneTriggerGate (true);
    assert (harness.process() > 1.0e-5f);

    harness.plugin.setStandaloneTriggerGate (false);
    assert (! harness.plugin.isStandaloneTriggerGateRequested());
    assert (harness.process() > 1.0e-5f);

    processUntilSilent (harness);
}
} // namespace

int main()
{
    testParameterSurfaceIsStableAndTriggerIsRuntimeOnly();
    testStateRoundTripAndMagicRejection();
    testStateRejectsMalformedRecords();
    testFreshPluginWithoutTriggersStaysSilentAndMeterOff();
    testProcessBlockRealtimeForbiddenPatternGuard();
    testHeldSyntheticTriggerRendersAndMeters();
    testRapidOnOffBeforeCallbackStillRendersRelease();
    testMidiNoteOffRestartsHeldStandaloneGate();
    testUiPressReleaseDoesNotInterruptHeldMidi();
    testHeldUiGateDoesNotInterruptMidiUntilMidiOff();
    testFlushDoesNotSuppressHeldStandaloneGate();
    testReleaseGateDecaysToSilence();

    std::cout << "BlackThrumPluginBridgeTests passed\n";
    return 0;
}
