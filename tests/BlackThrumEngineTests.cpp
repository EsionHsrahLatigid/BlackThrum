#include "blackthrum/BlackThrumEngine.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using blackthrum::BlackThrumEngine;
using blackthrum::BlackThrumParameters;

namespace
{
constexpr double sampleRate = 48000.0;

struct Rendered
{
    std::vector<float> left;
    std::vector<float> right;
};

Rendered renderNote (std::uint32_t seed, int note, float velocity, BlackThrumParameters params, int samples)
{
    BlackThrumEngine engine;
    engine.prepare (sampleRate);
    engine.setParameters (params);
    engine.reset (seed);
    engine.noteOn (note, velocity);

    Rendered output;
    output.left.reserve (static_cast<std::size_t> (samples));
    output.right.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
    {
        const auto frame = engine.processSample();
        output.left.push_back (frame.left);
        output.right.push_back (frame.right);
    }
    return output;
}

float sumSquares (const std::vector<float>& samples)
{
    float energy = 0.0f;
    for (const auto sample : samples)
        energy += sample * sample;
    return energy;
}

float rms (const std::vector<float>& samples)
{
    return std::sqrt (sumSquares (samples) / static_cast<float> (samples.size()));
}

float peak (const Rendered& rendered)
{
    float result = 0.0f;
    for (std::size_t i = 0; i < rendered.left.size(); ++i)
        result = std::max (result, std::max (std::fabs (rendered.left[i]), std::fabs (rendered.right[i])));
    return result;
}

float maxDifference (const Rendered& a, const Rendered& b)
{
    float result = 0.0f;
    for (std::size_t i = 0; i < a.left.size(); ++i)
    {
        result = std::max (result, std::fabs (a.left[i] - b.left[i]));
        result = std::max (result, std::fabs (a.right[i] - b.right[i]));
    }
    return result;
}

float channelCorrelation (const Rendered& rendered)
{
    float ll = 0.0f;
    float rr = 0.0f;
    float lr = 0.0f;
    for (std::size_t i = 512; i < rendered.left.size(); ++i)
    {
        ll += rendered.left[i] * rendered.left[i];
        rr += rendered.right[i] * rendered.right[i];
        lr += rendered.left[i] * rendered.right[i];
    }

    return lr / std::sqrt (std::max (ll * rr, 1.0e-12f));
}

int zeroCrossings (const std::vector<float>& samples)
{
    int count = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        if ((samples[i - 1] < 0.0f && samples[i] >= 0.0f) || (samples[i - 1] >= 0.0f && samples[i] < 0.0f))
            ++count;
    return count;
}

void assertFiniteBounded (const Rendered& rendered)
{
    for (std::size_t i = 0; i < rendered.left.size(); ++i)
    {
        assert (std::isfinite (rendered.left[i]));
        assert (std::isfinite (rendered.right[i]));
        assert (rendered.left[i] >= -0.98f && rendered.left[i] <= 0.98f);
        assert (rendered.right[i] >= -0.98f && rendered.right[i] <= 0.98f);
    }
}

void testSilentBeforeTrigger()
{
    BlackThrumEngine engine;
    engine.prepare (sampleRate);
    engine.reset (123u);

    for (int i = 0; i < 2048; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}

void testDefaultRendersFiniteBoundedEnergy()
{
    const auto rendered = renderNote (1u, 36, 1.0f, {}, 8192);
    assertFiniteBounded (rendered);
    assert (peak (rendered) <= 0.98f);
    assert (rms (rendered.left) >= 1.0e-4f);
}

void testDeterministicSameEvents()
{
    BlackThrumParameters params;
    params.thrum = 0.83f;
    params.drift = 0.77f;
    params.formant = 0.61f;
    params.width = 0.73f;
    params.grind = 0.58f;

    const auto a = renderNote (4242u, 36, 0.8f, params, 4096);
    const auto b = renderNote (4242u, 36, 0.8f, params, 4096);

    assert (maxDifference (a, b) <= 1.0e-6f);
}

void testNoteRootsCarrierFrequency()
{
    BlackThrumParameters params;
    params.drift = 0.0f;
    params.width = 0.0f;
    params.grind = 0.18f;

    const auto lowNote = renderNote (99u, 36, 1.0f, params, 12000);
    const auto highNote = renderNote (99u, 48, 1.0f, params, 12000);

    assert (zeroCrossings (highNote.left) > zeroCrossings (lowNote.left) * 17 / 10);
}

void testThreeCarrierFormantAndGrindIdentity()
{
    BlackThrumParameters plain;
    plain.thrum = 0.0f;
    plain.formant = 0.0f;
    plain.grind = 0.0f;
    plain.drift = 0.0f;

    BlackThrumParameters dense = plain;
    dense.thrum = 1.0f;
    dense.formant = 1.0f;
    dense.grind = 1.0f;

    const auto a = renderNote (55u, 41, 1.0f, plain, 8192);
    const auto b = renderNote (55u, 41, 1.0f, dense, 8192);

    assert (maxDifference (a, b) > 0.01f);
    assert (rms (b.left) > rms (a.left) * 1.15f);
}

void testSeededDriftAndStereoWidthDecorrelateChannels()
{
    BlackThrumParameters wide;
    wide.drift = 1.0f;
    wide.width = 1.0f;
    wide.thrum = 0.8f;

    BlackThrumParameters narrow = wide;
    narrow.width = 0.0f;

    const auto wideRender = renderNote (777u, 31, 1.0f, wide, 24000);
    const auto narrowRender = renderNote (777u, 31, 1.0f, narrow, 24000);

    assert (channelCorrelation (wideRender) < 0.985f);
    assert (std::fabs (channelCorrelation (narrowRender)) > channelCorrelation (wideRender));
    assert (maxDifference (wideRender, narrowRender) > 0.005f);
}

void testReleaseTailReachesGate()
{
    BlackThrumEngine engine;
    engine.prepare (sampleRate);
    engine.reset (777u);
    engine.noteOn (40, 1.0f);

    for (int i = 0; i < 1024; ++i)
        (void) engine.processSample();

    engine.noteOff (40);

    bool heardTail = false;
    for (int i = 0; i < 120000; ++i)
    {
        const auto frame = engine.processSample();
        heardTail = heardTail || std::fabs (frame.left) > 1.0e-5f || std::fabs (frame.right) > 1.0e-5f;
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
    }

    assert (heardTail);
    for (int i = 0; i < 4096; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::fabs (frame.left) <= 1.0e-5f);
        assert (std::fabs (frame.right) <= 1.0e-5f);
    }
}

void testFiniteBoundedExtremeParameters()
{
    BlackThrumParameters params;
    params.pitchOffset = 1000.0f;
    params.thrum = 1000.0f;
    params.drift = 1000.0f;
    params.formant = 1000.0f;
    params.width = 1000.0f;
    params.grind = 1000.0f;
    params.outputGain = 1000.0f;

    BlackThrumEngine engine;
    engine.prepare (0.0);
    engine.setParameters (params);
    engine.reset (0u);
    engine.noteOn (999, 1000.0f);

    for (int i = 0; i < 8192; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.98f && frame.left <= 0.98f);
        assert (frame.right >= -0.98f && frame.right <= 0.98f);
    }
}

void testNonFiniteParametersFallbackSafely()
{
    BlackThrumParameters params;
    params.pitchOffset = std::numeric_limits<float>::quiet_NaN();
    params.thrum = std::numeric_limits<float>::infinity();
    params.drift = -std::numeric_limits<float>::infinity();
    params.formant = std::numeric_limits<float>::quiet_NaN();
    params.width = std::numeric_limits<float>::infinity();
    params.grind = std::numeric_limits<float>::quiet_NaN();
    params.outputGain = std::numeric_limits<float>::quiet_NaN();

    BlackThrumEngine engine;
    engine.prepare (std::numeric_limits<double>::infinity());
    engine.setParameters (params);
    engine.reset (31337u);
    engine.noteOn (-100, std::numeric_limits<float>::infinity());

    bool sawEnergy = false;
    for (int i = 0; i < 4096; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.98f && frame.left <= 0.98f);
        assert (frame.right >= -0.98f && frame.right <= 0.98f);
        sawEnergy = sawEnergy || std::fabs (frame.left) > 1.0e-6f || std::fabs (frame.right) > 1.0e-6f;
    }

    assert (sawEnergy);
}

void testVelocityZeroActsSilent()
{
    BlackThrumEngine engine;
    engine.prepare (sampleRate);
    engine.reset (4u);
    engine.noteOn (44, 0.0f);

    for (int i = 0; i < 1024; ++i)
    {
        const auto frame = engine.processSample();
        assert (frame.left == 0.0f);
        assert (frame.right == 0.0f);
    }
}

void testVeryLowSampleRateFallsBackSafely()
{
    BlackThrumEngine engine;
    engine.prepare (1.5);
    engine.noteOn (40, 1.0f);
    for (int i = 0; i < 512; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
    }
}

} // namespace

int main()
{
    testSilentBeforeTrigger();
    testDefaultRendersFiniteBoundedEnergy();
    testDeterministicSameEvents();
    testNoteRootsCarrierFrequency();
    testThreeCarrierFormantAndGrindIdentity();
    testSeededDriftAndStereoWidthDecorrelateChannels();
    testReleaseTailReachesGate();
    testFiniteBoundedExtremeParameters();
    testNonFiniteParametersFallbackSafely();
    testVelocityZeroActsSilent();
    testVeryLowSampleRateFallsBackSafely();

    std::cout << "BlackThrumEngineTests passed\n";
    return 0;
}
