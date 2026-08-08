#include "blackthrum/BlackThrumEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace blackthrum
{

namespace
{
constexpr float ceiling = 0.98f;
constexpr float attackSeconds = 0.006f;
constexpr float releaseSeconds = 0.18f;
constexpr float denormalNoise = 1.0e-24f;

float midiNoteToHz (int noteNumber, float pitchOffset) noexcept
{
    const auto semitones = (static_cast<float> (noteNumber) + pitchOffset - 69.0f) / 12.0f;
    return 440.0f * std::pow (2.0f, semitones);
}

float wrapPhase (float phase) noexcept
{
    phase -= std::floor (phase);
    return phase;
}

float sine (float phase) noexcept
{
    return std::sin (phase * 2.0f * std::numbers::pi_v<float>);
}
} // namespace

BlackThrumEngine::BlackThrumEngine()
{
    prepare (44100.0);
    reset (1u);
}

void BlackThrumEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0 ? newSampleRate : 44100.0;
    dcLeft.prepare (sampleRate, 2.0f);
    dcRight.prepare (sampleRate, 2.0f);
    updateEnvelopeRates();
    updateCarrierTargets();
}

void BlackThrumEngine::reset (std::uint32_t seed) noexcept
{
    baseSeed = seed != 0u ? seed : 1u;
    currentNote = -1;
    rootFrequency = midiNoteToHz (36, params.pitchOffset);
    envelopeStage = EnvelopeStage::idle;
    envelope = 0.0f;
    velocity = 0.0f;
    widthPhase = 0.0f;

    driftNoise.reset (mixSeed (baseSeed ^ 0x4219f37du));
    for (int i = 0; i < 3; ++i)
    {
        carrierPhase[i] = 0.0f;
        driftState[i] = 0.0f;
    }

    dcLeft.reset();
    dcRight.reset();
    updateCarrierTargets();
}

void BlackThrumEngine::setParameters (const BlackThrumParameters& parameters) noexcept
{
    params.pitchOffset = clampFinite (parameters.pitchOffset, -24.0f, 24.0f, BlackThrumParameters {}.pitchOffset);
    params.thrum = clampFinite (parameters.thrum, 0.0f, 1.0f, BlackThrumParameters {}.thrum);
    params.drift = clampFinite (parameters.drift, 0.0f, 1.0f, BlackThrumParameters {}.drift);
    params.formant = clampFinite (parameters.formant, 0.0f, 1.0f, BlackThrumParameters {}.formant);
    params.width = clampFinite (parameters.width, 0.0f, 1.0f, BlackThrumParameters {}.width);
    params.grind = clampFinite (parameters.grind, 0.0f, 1.0f, BlackThrumParameters {}.grind);
    params.outputGain = clampFinite (parameters.outputGain, 0.0f, 2.0f, BlackThrumParameters {}.outputGain);

    if (currentNote >= 0)
        rootFrequency = midiNoteToHz (currentNote, params.pitchOffset);
    updateCarrierTargets();
}

void BlackThrumEngine::noteOn (int noteNumber, float newVelocity) noexcept
{
    const auto incomingNote = clampNote (noteNumber);
    const auto incomingVelocity = clampFinite (newVelocity, 0.0f, 1.0f, 1.0f);

    if (incomingVelocity <= 0.0f)
    {
        noteOff (incomingNote);
        return;
    }

    currentNote = incomingNote;
    velocity = incomingVelocity;
    rootFrequency = midiNoteToHz (currentNote, params.pitchOffset);

    const auto noteSeed = mixSeed (baseSeed ^ (static_cast<std::uint32_t> (currentNote) * 0x45d9f3bu));
    driftNoise.reset (mixSeed (noteSeed ^ 0x7f4a7c15u));
    for (int i = 0; i < 3; ++i)
    {
        carrierPhase[i] = static_cast<float> ((mixSeed (noteSeed + static_cast<std::uint32_t> (i) * 0x9e3779b9u) >> 8u) & 0xffffu) / 65536.0f;
        driftState[i] = 0.0f;
    }
    widthPhase = 0.0f;

    envelope = 0.0f;
    envelopeStage = EnvelopeStage::attack;
    dcLeft.reset();
    dcRight.reset();
    updateCarrierTargets();
}

void BlackThrumEngine::noteOff (int noteNumber) noexcept
{
    const auto safeNote = clampNote (noteNumber);
    if (currentNote == safeNote && envelopeStage != EnvelopeStage::idle)
        envelopeStage = EnvelopeStage::release;
}

StereoFrame BlackThrumEngine::processSample() noexcept
{
    const auto envelopeValue = processEnvelope();
    if (envelopeValue <= 0.0f)
        return {};

    const auto drift0 = nextDrift (0);
    const auto drift1 = nextDrift (1);
    const auto drift2 = nextDrift (2);

    const auto c0 = renderCarrier (0, drift0);
    const auto c1 = renderCarrier (1, drift1);
    const auto c2 = renderCarrier (2, drift2);

    const auto formantCentre = 1.35f + params.formant * 2.9f;
    const auto skewA = std::sin ((c0 + 0.35f * c2) * formantCentre);
    const auto skewB = std::sin ((c1 - 0.22f * c0) * (formantCentre * 1.71f));
    const auto coupled = c0 * 0.46f
                       + c1 * (0.28f + 0.16f * params.thrum)
                       + c2 * 0.22f
                       + skewA * (0.10f + 0.24f * params.formant)
                       + skewB * (0.06f + 0.18f * params.grind);

    const auto grindDrive = 1.0f + params.grind * 9.0f + params.thrum * 2.4f;
    const auto ground = boundedDrive (coupled + c0 * c1 * (0.20f + params.formant * 0.35f), grindDrive);

    widthPhase = wrapPhase (widthPhase + (0.043f + params.drift * 0.091f) / static_cast<float> (sampleRate));
    const auto widthWobble = 0.72f + 0.28f * sine (widthPhase + 0.17f * drift2);
    const auto sideAmount = params.width * widthWobble;
    const auto side = (c0 * 0.52f - c1 * 0.36f + c2 * 0.24f + drift0 * 0.10f) * sideAmount;
    const auto mono = ground * (0.54f + 0.42f * params.thrum);
    const auto gain = envelopeValue * params.outputGain * 0.58f;

    const auto left = dcLeft.process ((mono + side) * gain + denormalNoise);
    const auto right = dcRight.process ((mono - side * 0.92f) * gain - denormalNoise);
    return sanitizeFrame (left, right);
}

void BlackThrumEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample();
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

std::uint32_t BlackThrumEngine::mixSeed (std::uint32_t value) noexcept
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value != 0u ? value : 0x6d2b79f5u;
}

int BlackThrumEngine::clampNote (int noteNumber) noexcept
{
    return std::clamp (noteNumber, 0, 127);
}

void BlackThrumEngine::updateEnvelopeRates() noexcept
{
    attackStep = 1.0f / static_cast<float> (std::max (1.0, sampleRate * static_cast<double> (attackSeconds)));
    releaseCoefficient = std::exp (-6.90775527898f / static_cast<float> (std::max (1.0, sampleRate * static_cast<double> (releaseSeconds))));
}

void BlackThrumEngine::updateCarrierTargets() noexcept
{
    const auto safeRoot = clampFinite (rootFrequency, 8.0f, static_cast<float> (sampleRate * 0.18), 65.40639f);
    const auto detune = 0.004f + params.thrum * 0.031f;
    carrierFrequency[0] = safeRoot;
    carrierFrequency[1] = safeRoot * (1.0f + detune);
    carrierFrequency[2] = safeRoot * (1.4983f - detune * 0.67f);
}

float BlackThrumEngine::processEnvelope() noexcept
{
    if (envelopeStage == EnvelopeStage::attack)
    {
        envelope += attackStep;
        if (envelope >= 1.0f)
        {
            envelope = 1.0f;
            envelopeStage = EnvelopeStage::hold;
        }
    }
    else if (envelopeStage == EnvelopeStage::release)
    {
        envelope *= releaseCoefficient;
        if (envelope < 1.0e-5f)
        {
            envelope = 0.0f;
            velocity = 0.0f;
            currentNote = -1;
            envelopeStage = EnvelopeStage::idle;
        }
    }

    return envelope * velocity;
}

float BlackThrumEngine::nextDrift (int carrier) noexcept
{
    const auto target = driftNoise.nextFloat();
    const auto coefficient = 0.9992f - params.drift * 0.00055f;
    driftState[carrier] = flushDenormal (driftState[carrier] * coefficient + target * (1.0f - coefficient));
    return driftState[carrier] * params.drift;
}

float BlackThrumEngine::renderCarrier (int carrier, float driftMod) noexcept
{
    const auto vibrato = 1.0f + driftMod * (0.006f + params.thrum * 0.012f);
    const auto increment = std::clamp (carrierFrequency[carrier] * vibrato / static_cast<float> (sampleRate), 0.0f, 0.45f);
    carrierPhase[carrier] = wrapPhase (carrierPhase[carrier] + increment);

    const auto fundamental = sine (carrierPhase[carrier]);
    const auto fold = sine (carrierPhase[carrier] * (2.0f + 0.31f * static_cast<float> (carrier)));
    return boundedDrive (fundamental + fold * (0.12f + params.thrum * 0.18f), 1.0f + params.grind * 1.8f);
}

StereoFrame BlackThrumEngine::sanitizeFrame (float left, float right) const noexcept
{
    const auto safeLeft = flushDenormal (boundedDrive (left, 1.18f));
    const auto safeRight = flushDenormal (boundedDrive (right, 1.18f));
    return { std::clamp (safeLeft, -ceiling, ceiling),
             std::clamp (safeRight, -ceiling, ceiling) };
}

} // namespace blackthrum
