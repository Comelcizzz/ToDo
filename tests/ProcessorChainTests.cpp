#include "mastering/dsp/ProcessorChain.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE("Processor chain respects clip ceiling", "[dsp]")
{
    std::vector<float> left(2'048, 1.5f);
    std::vector<float> right(2'048, -1.5f);
    float* channels[] {left.data(), right.data()};

    mastering::dsp::ProcessorSettings settings;
    settings.equalizer.highPassHz = 10.0;
    settings.compressor.thresholdDb = 0.0;
    settings.saturation = 0.0;
    settings.clipCeilingDb = -1.0;
    settings.amount = 1.0;

    mastering::dsp::ProcessorChain chain;
    chain.prepare(48'000.0);
    chain.setSettings(settings);
    chain.process(channels, 2, static_cast<int>(left.size()));

    const auto ceiling = mastering::dsp::dbToGain(-1.0);
    const auto maximum = std::ranges::max(left);
    CHECK(maximum <= ceiling + 1.0e-5);
}

TEST_CASE("Bypassed processor leaves audio unchanged", "[dsp]")
{
    std::vector<float> samples {0.1f, -0.25f, 0.4f};
    const auto original = samples;
    float* channels[] {samples.data()};

    mastering::dsp::ProcessorSettings settings;
    settings.bypass = true;

    mastering::dsp::ProcessorChain chain;
    chain.prepare(48'000.0);
    chain.setSettings(settings);
    chain.process(channels, 1, static_cast<int>(samples.size()));

    CHECK(samples == original);
}
