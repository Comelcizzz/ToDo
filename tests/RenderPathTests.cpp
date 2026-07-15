#include "mastering/assistant/MixAdvisor.h"
#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/ProcessorChain.h"
#include "mastering/project/ProjectDocument.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

TEST_CASE("True-peak limiter enforces ceiling", "[dsp]")
{
    std::vector<float> left(4'096, 1.2f);
    std::vector<float> right(4'096, -1.2f);
    float* channels[] {left.data(), right.data()};

    mastering::dsp::TruePeakLimiter limiter;
    mastering::dsp::TruePeakLimiterSettings settings;
    settings.ceilingDbTp = -1.0;
    settings.lookAheadMs = 1.5;
    settings.oversamplingFactor = 4;
    limiter.prepare(48'000.0, 2048, 2, 4);
    limiter.setSettings(settings);
    for (int off = 0; off < 4096; off += 512) {
        float* slice[] {left.data() + off, right.data() + off};
        limiter.process(slice, 2, 512);
    }

    const auto ceiling = mastering::dsp::dbToGain(-1.0);
    // Skip latency prefix.
    const auto lat = limiter.latencySamples();
    float peak = 0.0f;
    for (int i = lat; i < 4096; ++i) {
        peak = std::max(peak, std::abs(left[static_cast<std::size_t>(i)]));
        peak = std::max(peak, std::abs(right[static_cast<std::size_t>(i)]));
    }
    CHECK(peak <= ceiling + 0.05f);
}

TEST_CASE("Dynamic separator ducks target from sidechain", "[dsp]")
{
    std::vector<float> sidechain(512, 0.8f);
    std::vector<float> target(512, 0.5f);
    const float* side[] {sidechain.data()};
    float* dest[] {target.data()};

    mastering::dsp::DynamicSeparator separator;
    separator.prepare(48'000.0);
    separator.setDepthDb(6.0);
    separator.setAttackMs(0.1);
    separator.setReleaseMs(50.0);
    separator.process(side, dest, 1, static_cast<int>(target.size()));

    CHECK(target.back() < 0.5f);
}

TEST_CASE("Mix advisor exposes three bounded variants", "[assistant]")
{
    mastering::project::ProjectDocument project;
    project.id = "variants";

    mastering::project::TrackRecord kick;
    kick.id = "kick";
    kick.role = mastering::project::TrackRole::kick;
    kick.metrics.rmsDbfs = -24.0;

    mastering::project::TrackRecord vocal;
    vocal.id = "vocal";
    vocal.role = mastering::project::TrackRole::cleanVocal;
    vocal.metrics.rmsDbfs = -24.0;

    project.tracks = {kick, vocal};
    const mastering::assistant::MixAdvisor advisor;
    const auto variants = advisor.createVariants(project);

    REQUIRE(variants.size() == 3);
    CHECK(variants[0].variantLabel == "balanced");
    CHECK(variants[1].variantLabel == "punchy");
    CHECK(variants[2].variantLabel == "vocal-forward");
}
