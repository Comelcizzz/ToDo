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
    std::vector<float> left(1'024, 1.2f);
    std::vector<float> right(1'024, -1.2f);
    float* channels[] {left.data(), right.data()};

    mastering::dsp::TruePeakLimiter limiter;
    limiter.prepare(48'000.0);
    limiter.setCeilingDb(-1.0);
    limiter.process(channels, 2, static_cast<int>(left.size()));

    const auto ceiling = mastering::dsp::dbToGain(-1.0);
    const auto peak = std::max(
        *std::max_element(left.begin(), left.end()),
        -*std::min_element(right.begin(), right.end()));
    CHECK(peak <= ceiling + 0.02f);
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
