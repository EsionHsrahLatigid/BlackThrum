#pragma once

#include "blackthrum/BlackThrumDspPrimitives.h"

#include <cstdint>

namespace blackthrum
{

/** Realtime-safe parameter set for the BlackThrum three-carrier thrum instrument.

    All values are sanitized by setParameters():
    pitchOffset [-24, 24] semitones, thrum [0, 1], drift [0, 1],
    formant [0, 1], width [0, 1], grind [0, 1], outputGain [0, 2].
*/
struct BlackThrumParameters
{
    float pitchOffset = 0.0f;
    float thrum = 0.68f;
    float drift = 0.42f;
    float formant = 0.52f;
    float width = 0.44f;
    float grind = 0.38f;
    float outputGain = 0.72f;
};

/** Monophonic MIDI-like three-carrier drone instrument.

    noteOn() restarts a velocity-sensitive attack/hold/release envelope. The
    MIDI note number sets the rooted carrier frequencies. processSample() and
    process() allocate no memory and always return finite, ceiling-bounded samples.
*/
class BlackThrumEngine
{
public:
    BlackThrumEngine();

    /** Sets the sample rate and rebuilds filters; invalid rates fall back to 44.1 kHz. */
    void prepare (double sampleRate) noexcept;

    /** Clears state and sets the deterministic base seed used by future noteOn calls. */
    void reset (std::uint32_t seed = 1u) noexcept;

    /** Applies sanitized parameters and updates filter/envelope coefficients. */
    void setParameters (const BlackThrumParameters& parameters) noexcept;

    /** Starts or retriggers the monophonic note, with velocity clamped to [0, 1]. */
    void noteOn (int noteNumber, float velocity) noexcept;

    /** Releases the current note; mismatched note numbers are ignored while another note is held. */
    void noteOff (int noteNumber) noexcept;

    /** Renders one stereo frame. Silent before noteOn and after envelope decay. */
    [[nodiscard]] StereoFrame processSample() noexcept;

    /** Renders numSamples into stereo buffers when both pointers are valid. */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    enum class EnvelopeStage
    {
        idle,
        attack,
        hold,
        release
    };

    struct ClampedParameters
    {
        float pitchOffset = 0.0f;
        float thrum = 0.68f;
        float drift = 0.42f;
        float formant = 0.52f;
        float width = 0.44f;
        float grind = 0.38f;
        float outputGain = 0.72f;
    };

    static std::uint32_t mixSeed (std::uint32_t value) noexcept;
    static int clampNote (int noteNumber) noexcept;

    void updateEnvelopeRates() noexcept;
    void updateCarrierTargets() noexcept;
    [[nodiscard]] float processEnvelope() noexcept;
    [[nodiscard]] float nextDrift (int carrier) noexcept;
    [[nodiscard]] float renderCarrier (int carrier, float driftMod) noexcept;
    [[nodiscard]] StereoFrame sanitizeFrame (float left, float right) const noexcept;

    ClampedParameters params;
    double sampleRate = 44100.0;
    std::uint32_t baseSeed = 1u;
    int currentNote = -1;
    float rootFrequency = 65.40639f;

    DeterministicNoise driftNoise;
    DcBlocker dcLeft;
    DcBlocker dcRight;

    EnvelopeStage envelopeStage = EnvelopeStage::idle;
    float envelope = 0.0f;
    float velocity = 0.0f;
    float attackStep = 1.0f;
    float releaseCoefficient = 0.999f;

    float carrierPhase[3] {};
    float carrierFrequency[3] {};
    float driftState[3] {};
    float widthPhase = 0.0f;
};

} // namespace blackthrum
