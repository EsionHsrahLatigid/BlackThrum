#include "BlackThrumPlugin.h"

#include "ProductState.h"

#if ! BLACKTHRUM_HEADLESS_TEST
#include "ParameterGridEditor.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace blackthrum::plugin
{
namespace
{
constexpr std::array<char, 4> stateMagic {{ 'B', 'L', 'T', '1' }};
constexpr int stateVersion = 1;
constexpr int controlUpdatePeriod = 16;
constexpr int standaloneTriggerNote = 36;
constexpr float standaloneTriggerVelocity = 1.0f;

yup::NormalisableRange<float> makePitchOffsetRange()
{
    auto range = yup::NormalisableRange<float> (-24.0f, 24.0f);
    return range;
}

constexpr std::array<std::array<float, 7>, 4> presetValues {{
    {{ 0.0f, 0.68f, 0.42f, 0.52f, 0.44f, 0.38f, 0.72f }},
    {{ -12.0f, 0.91f, 0.64f, 0.34f, 0.58f, 0.62f, 0.70f }},
    {{ 7.0f, 0.46f, 0.30f, 0.88f, 0.28f, 0.71f, 0.66f }},
    {{ -5.0f, 0.82f, 0.86f, 0.67f, 0.95f, 0.49f, 0.62f }}
}};

float sanitizeVelocity (const yup::MidiMessage& message) noexcept
{
    return std::clamp (message.getFloatVelocity(), 0.0f, 1.0f);
}
} // namespace

BlackThrumPlugin::BlackThrumPlugin()
    : yup::AudioProcessor ("BlackThrum",
                           yup::AudioBusLayout ({
                                                    yup::AudioBus ("midi", yup::AudioBus::Midi, yup::AudioBus::Input, 1),
                                                },
                                                {
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2),
                                                }))
{
    parameters[pitchOffset] = yup::AudioParameterBuilder()
                                  .withID ("pitch_offset")
                                  .withName ("Pitch offset")
                                  .withHostID (pitchOffset)
                                  .withRange (makePitchOffsetRange())
                                  .withDefault (presetValues[0][pitchOffset])
                                  .withSmoothing (35.0f)
                                  .withModulatable (true)
                                  .build();
    parameters[thrum] = yup::AudioParameterBuilder()
                            .withID ("thrum")
                            .withName ("Thrum")
                            .withHostID (thrum)
                            .withRange (0.0f, 1.0f)
                            .withDefault (presetValues[0][thrum])
                            .withSmoothing (25.0f)
                            .withModulatable (true)
                            .build();
    parameters[drift] = yup::AudioParameterBuilder()
                            .withID ("drift")
                            .withName ("Drift")
                            .withHostID (drift)
                            .withRange (0.0f, 1.0f)
                            .withDefault (presetValues[0][drift])
                            .withSmoothing (45.0f)
                            .withModulatable (true)
                            .build();
    parameters[formant] = yup::AudioParameterBuilder()
                              .withID ("formant")
                              .withName ("Formant")
                              .withHostID (formant)
                              .withRange (0.0f, 1.0f)
                              .withDefault (presetValues[0][formant])
                              .withSmoothing (30.0f)
                              .withModulatable (true)
                              .build();
    parameters[width] = yup::AudioParameterBuilder()
                            .withID ("width")
                            .withName ("Width")
                            .withHostID (width)
                            .withRange (0.0f, 1.0f)
                            .withDefault (presetValues[0][width])
                            .withSmoothing (25.0f)
                            .withModulatable (true)
                            .build();
    parameters[grind] = yup::AudioParameterBuilder()
                            .withID ("grind")
                            .withName ("Grind")
                            .withHostID (grind)
                            .withRange (0.0f, 1.0f)
                            .withDefault (presetValues[0][grind])
                            .withSmoothing (20.0f)
                            .withModulatable (true)
                            .build();
    parameters[output] = yup::AudioParameterBuilder()
                             .withID ("output")
                             .withName ("Output")
                             .withHostID (output)
                             .withRange (0.0f, 2.0f)
                             .withDefault (presetValues[0][output])
                             .withSmoothing (30.0f)
                             .withModulatable (true)
                             .build();

    for (const auto& parameter : parameters)
        addParameter (parameter);
}

void BlackThrumPlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (spec.sampleRate);

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], spec.sampleRate);
        smoothedValues[i] = parameters[i]->getValue();
    }

    engine.reset();
    applyEngineParameters();
    resetPerformanceState();
}

void BlackThrumPlugin::releaseResources()
{
}

void BlackThrumPlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    auto midi = context.midi.begin();
    const auto midiEnd = context.midi.end();
    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;
    float blockPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        consumeStandaloneTriggerGate();

        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            const auto& message = (*midi).getMessage();
            if (message.isNoteOn())
            {
                activeNote = std::clamp (message.getNoteNumber(), 0, 127);
                activeSource = ActiveSource::midi;
                engine.noteOn (activeNote, sanitizeVelocity (message));
            }
            else if (message.isNoteOff())
            {
                const auto note = std::clamp (message.getNoteNumber(), 0, 127);
                if (activeSource == ActiveSource::midi && note == activeNote)
                {
                    engine.noteOff (note);
                    if (standaloneTriggerDesiredGate.load (std::memory_order_relaxed) != 0)
                    {
                        activeNote = standaloneTriggerNote;
                        activeSource = ActiveSource::standalone;
                        engine.noteOn (standaloneTriggerNote, standaloneTriggerVelocity);
                    }
                    else
                    {
                        activeNote = -1;
                        activeSource = ActiveSource::none;
                    }
                }
            }
            ++midi;
        }

        advanceParameterHandles (sample);
        if (controlUpdateCountdown <= 0)
        {
            applyEngineParameters();
            controlUpdateCountdown = controlUpdatePeriod;
        }
        --controlUpdateCountdown;

        const auto frame = engine.processSample();

        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;
        blockPeak = std::max (blockPeak, std::max (std::fabs (frame.left), std::fabs (frame.right)));

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

    outputPeakMilli.store (static_cast<int> (std::clamp (blockPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                           std::memory_order_relaxed);
    context.midi.clear();
}

void BlackThrumPlugin::flush()
{
    engine.reset();
    resetPerformanceState();
}

bool BlackThrumPlugin::acceptsMidi() const noexcept
{
    return true;
}

int BlackThrumPlugin::getNumVoices() const
{
    return 1;
}

int BlackThrumPlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void BlackThrumPlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);
}

int BlackThrumPlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String BlackThrumPlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void BlackThrumPlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result BlackThrumPlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    auto presetIndex = currentPreset.load (std::memory_order_relaxed);
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), presetIndex);
    if (result.wasOk())
        currentPreset.store (presetIndex, std::memory_order_relaxed);
    return result;
}

yup::Result BlackThrumPlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool BlackThrumPlugin::hasEditor() const
{
#if BLACKTHRUM_HEADLESS_TEST
    return false;
#else
    return true;
#endif
}

yup::AudioProcessorEditor* BlackThrumPlugin::createEditor()
{
#if BLACKTHRUM_HEADLESS_TEST
    return nullptr;
#else
    return new ParameterGridEditor (*this,
                                    "BlackThrum",
                                    "Hold Trigger or Space to play. External MIDI takes priority.",
                                    0xfff2f2f0u);
#endif
}

void BlackThrumPlugin::setStandaloneTriggerGate (bool shouldBeOn) noexcept
{
    const auto newValue = shouldBeOn ? 1 : 0;
    const auto oldValue = standaloneTriggerDesiredGate.exchange (newValue, std::memory_order_relaxed);
    if (oldValue != newValue)
        standaloneTriggerGateEdges.fetch_add (1u, std::memory_order_release);
}

bool BlackThrumPlugin::isStandaloneTriggerGateRequested() const noexcept
{
    return standaloneTriggerDesiredGate.load (std::memory_order_relaxed) != 0;
}

float BlackThrumPlugin::getOutputPeakLevel() const noexcept
{
    return static_cast<float> (outputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

std::uint32_t BlackThrumPlugin::getStandaloneTriggerEdgeCountForTests() const noexcept
{
    return standaloneTriggerGateEdges.load (std::memory_order_acquire);
}

void BlackThrumPlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        smoothedValues[i] = parameterHandles[i].getNextValue();
    }
}

void BlackThrumPlugin::consumeStandaloneTriggerGate() noexcept
{
    const auto publishedEdges = standaloneTriggerGateEdges.load (std::memory_order_acquire);
    if (publishedEdges == consumedStandaloneGateEdges)
        return;

    ++consumedStandaloneGateEdges;
    audioStandaloneGate = ! audioStandaloneGate;

    if (activeSource == ActiveSource::midi)
        return;

    if (audioStandaloneGate)
    {
        activeNote = standaloneTriggerNote;
        activeSource = ActiveSource::standalone;
        engine.noteOn (standaloneTriggerNote, standaloneTriggerVelocity);
    }
    else if (activeSource == ActiveSource::standalone)
    {
        engine.noteOff (standaloneTriggerNote);
        activeNote = -1;
        activeSource = ActiveSource::none;
    }
}

void BlackThrumPlugin::applyEngineParameters() noexcept
{
    blackthrum::BlackThrumParameters engineParameters;
    engineParameters.pitchOffset = smoothedValues[pitchOffset];
    engineParameters.thrum = smoothedValues[thrum];
    engineParameters.drift = smoothedValues[drift];
    engineParameters.formant = smoothedValues[formant];
    engineParameters.width = smoothedValues[width];
    engineParameters.grind = smoothedValues[grind];
    engineParameters.outputGain = smoothedValues[output];
    engine.setParameters (engineParameters);
}

void BlackThrumPlugin::resetPerformanceState() noexcept
{
    activeNote = -1;
    activeSource = ActiveSource::none;
    audioStandaloneGate = false;
    const auto publishedEdges = standaloneTriggerGateEdges.load (std::memory_order_acquire);
    consumedStandaloneGateEdges = standaloneTriggerDesiredGate.load (std::memory_order_relaxed) != 0 && publishedEdges > 0u
                                      ? publishedEdges - 1u
                                      : publishedEdges;
    controlUpdateCountdown = 0;
    outputPeakMilli.store (0, std::memory_order_relaxed);
}

} // namespace blackthrum::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new blackthrum::plugin::BlackThrumPlugin();
}
