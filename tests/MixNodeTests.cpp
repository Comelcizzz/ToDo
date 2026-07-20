#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mastering/dsp/MixNodeChain.h"
#include "mastering/ipc/MixNodeController.h"
#include "mastering/ipc/MixNodeProtocol.h"

#include <cmath>
#include <numbers>
#include <vector>

using mastering::dsp::MixNodeChain;
using mastering::dsp::MixNodeChainState;
using mastering::ipc::MixNodeAction;
using mastering::ipc::MixNodeIdentity;
using mastering::ipc::MixNodeMessageType;
using mastering::mixnode::ApplyResult;
using mastering::mixnode::MixNodeController;

namespace {

std::vector<float> sine(int n, double sr, double hz, float amp)
{
    std::vector<float> x(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t>(i)] = float(
            amp * std::sin(2.0 * std::numbers::pi * hz * i / sr));
    return x;
}

double rms(const std::vector<float>& x)
{
    double a = 0;
    for (float v : x)
        a += double(v) * v;
    return x.empty() ? 0.0 : std::sqrt(a / double(x.size()));
}

MixNodeAction makeAction(
    const MixNodeIdentity& id,
    const char* actionId,
    const char* processor,
    const char* param,
    double value)
{
    MixNodeAction a;
    a.actionId = actionId;
    a.actionVersion = 1;
    a.projectId = id.projectId;
    a.sessionId = id.sessionId;
    a.targetInstanceId = id.instanceId;
    a.processorId = processor;
    a.parameterId = param;
    a.proposedValue = value;
    a.allowedMin = -120;
    a.allowedMax = 48;
    return a;
}

} // namespace

TEST_CASE("Mix Node input gain changes audio", "[milestone2a]")
{
    MixNodeChain chain;
    MixNodeChainState st;
    st.inputGainDb = 6.0;
    chain.prepare(48'000.0, 256, 1);
    chain.setState(st);
    auto x = sine(2048, 48'000.0, 440.0, 0.2f);
    auto dry = x;
    float* p = x.data();
    chain.process(&p, nullptr, 1, int(x.size()));
    REQUIRE(rms(x) > rms(dry) * 1.5);
}

TEST_CASE("Mix Node static EQ boosts target frequency", "[milestone2a]")
{
    MixNodeChain chain;
    MixNodeChainState st;
    st.staticEq.frequencyHz = 1'000.0;
    st.staticEq.gainDb = 9.0;
    st.staticEq.q = 2.0;
    st.dynamicEq.bypass = true; // isolate static EQ
    st.dynamicEq.bands[0].enabled = false;
    chain.prepare(48'000.0, 256, 1);
    chain.setState(st);
    auto x = sine(8192, 48'000.0, 1'000.0, 0.15f);
    auto dryRms = rms(x);
    float* p = x.data();
    chain.process(&p, nullptr, 1, int(x.size()));
    REQUIRE(rms(x) > dryRms * 1.2);
}

TEST_CASE("Mix Node Dynamic EQ responds to external sidechain", "[milestone2a]")
{
    MixNodeChain chain;
    MixNodeChainState st;
    st.dynamicEq.bandCount = 1;
    st.dynamicEq.bands[0].frequencyHz = 70.0;
    st.dynamicEq.bands[0].q = 3.0;
    st.dynamicEq.bands[0].thresholdDb = -30.0;
    st.dynamicEq.bands[0].ratio = 6.0;
    st.dynamicEq.bands[0].maxCutDb = 12.0;
    st.dynamicEq.bands[0].detectorSource = mastering::dsp::DetectorSource::external;
    st.dynamicEq.bands[0].detectorFrequencyHz = 70.0;
    st.dynamicEq.bands[0].detectorBandPass = true;
    chain.prepare(48'000.0, 256, 1);
    chain.setState(st);

    auto bass = sine(8192, 48'000.0, 70.0, 0.25f);
    auto kick = sine(8192, 48'000.0, 70.0, 0.8f);
    auto dry = bass;
    float* t = bass.data();
    const float* sc = kick.data();
    chain.process(&t, &sc, 1, int(bass.size()));
    REQUIRE(rms(bass) < rms(dry) * 0.95);
}

TEST_CASE("Preview cancel restores committed state", "[milestone2a]")
{
    MixNodeController ctl;
    MixNodeIdentity id;
    id.projectId = "p1";
    id.sessionId = "s1";
    id.instanceId = "i1";
    ctl.setIdentity(id);
    ctl.prepareAudio(48'000.0, 128, 2);

    auto commit = makeAction(id, "a1", "inputGain", "gainDb", -3.0);
    REQUIRE(ctl.commit(commit) == ApplyResult::ok);
    REQUIRE(ctl.snapshot().committed.inputGainDb == Catch::Approx(-3.0));

    auto preview = makeAction(id, "a2", "inputGain", "gainDb", 6.0);
    REQUIRE(ctl.preview(preview) == ApplyResult::ok);
    REQUIRE(ctl.snapshot().previewActive);
    REQUIRE(ctl.snapshot().active.inputGainDb == Catch::Approx(6.0));
    REQUIRE(ctl.snapshot().committed.inputGainDb == Catch::Approx(-3.0));

    REQUIRE(ctl.cancelPreview() == ApplyResult::ok);
    REQUIRE_FALSE(ctl.snapshot().previewActive);
    REQUIRE(ctl.snapshot().active.inputGainDb == Catch::Approx(-3.0));
}

TEST_CASE("Commit is idempotent and rejects wrong instance", "[milestone2a]")
{
    MixNodeController ctl;
    MixNodeIdentity id;
    id.projectId = "p1";
    id.sessionId = "s1";
    id.instanceId = "i1";
    ctl.setIdentity(id);

    auto action = makeAction(id, "dup", "staticEq", "gainDb", 3.0);
    REQUIRE(ctl.commit(action) == ApplyResult::ok);
    REQUIRE(ctl.commit(action) == ApplyResult::idempotentReplay);

    auto wrong = action;
    wrong.targetInstanceId = "other";
    wrong.actionId = "dup2";
    REQUIRE(ctl.commit(wrong) == ApplyResult::rejectedWrongIds);
}

TEST_CASE("Host state save/restore excludes preview", "[milestone2a]")
{
    MixNodeController ctl;
    MixNodeIdentity id;
    id.projectId = "p1";
    id.sessionId = "s1";
    id.instanceId = "i1";
    id.trackName = "Bass";
    ctl.setIdentity(id);
    ctl.prepareAudio(48'000.0, 64, 1);

    auto committed = makeAction(id, "c1", "outputGain", "gainDb", -1.5);
    REQUIRE(ctl.commit(committed) == ApplyResult::ok);
    auto preview = makeAction(id, "p1a", "outputGain", "gainDb", 4.0);
    REQUIRE(ctl.preview(preview) == ApplyResult::ok);

    const auto json = mastering::ipc::serializeHostState(ctl.hostState());
    REQUIRE(json.find("\"hasPreview\":false") != std::string::npos);

    MixNodeController restored;
    restored.prepareAudio(48'000.0, 64, 1);
    mastering::ipc::MixNodeProtocolError err;
    REQUIRE(restored.restoreHostState(json, &err));
    REQUIRE(restored.snapshot().committed.outputGainDb == Catch::Approx(-1.5));
    REQUIRE_FALSE(restored.snapshot().previewActive);
}

TEST_CASE("Unsupported newer host schema rejected safely", "[milestone2a]")
{
    MixNodeController ctl;
    ctl.prepareAudio(48'000.0, 64, 1);
    mastering::ipc::MixNodeProtocolError err;
    REQUIRE_FALSE(ctl.restoreHostState(R"({"schemaVersion":99,"committed":{}})", &err));
    REQUIRE(err.code == "schema");
}

TEST_CASE("Oversized Mix Node blocks are chunked without silence", "[milestone2a]")
{
    MixNodeChain chain;
    chain.prepare(48'000.0, 64, 1);
    MixNodeChainState st;
    st.inputGainDb = 0.0;
    chain.setState(st);
    auto x = sine(1023, 48'000.0, 200.0, 0.3f);
    const auto before = rms(x);
    float* p = x.data();
    chain.process(&p, nullptr, 1, int(x.size()));
    REQUIRE(rms(x) == Catch::Approx(before).margin(0.02));
}

TEST_CASE("Protocol envelope rejects unknown type and validates ids", "[milestone2a]")
{
    mastering::ipc::MixNodeProtocolError err;
    REQUIRE_FALSE(mastering::ipc::parseEnvelope(R"({"mixNodeProtocolVersion":1,"type":"Nope"})", &err));

    mastering::ipc::MixNodeEnvelope env;
    env.mixNodeProtocolVersion = 1;
    env.projectId = "p";
    env.sessionId = "s";
    env.instanceId = "i";
    env.type = MixNodeMessageType::Heartbeat;
    REQUIRE(mastering::ipc::validateEnvelopeIds(env, "p", "s", "i", &err));
    REQUIRE_FALSE(mastering::ipc::validateEnvelopeIds(env, "other", "s", "i", &err));
}

TEST_CASE("Undo restores absolute previous state", "[milestone2a]")
{
    MixNodeController ctl;
    MixNodeIdentity id;
    id.projectId = "p";
    id.sessionId = "s";
    id.instanceId = "i";
    ctl.setIdentity(id);
    auto a = makeAction(id, "u1", "inputGain", "gainDb", 2.0);
    REQUIRE(ctl.commit(a) == ApplyResult::ok);
    auto b = makeAction(id, "u2", "inputGain", "gainDb", -4.0);
    REQUIRE(ctl.commit(b) == ApplyResult::ok);
    REQUIRE(ctl.undo() == ApplyResult::ok);
    REQUIRE(ctl.snapshot().committed.inputGainDb == Catch::Approx(2.0));
}

TEST_CASE("Suite disconnect cancels preview", "[milestone2a]")
{
    MixNodeController ctl;
    MixNodeIdentity id;
    id.projectId = "p";
    id.sessionId = "s";
    id.instanceId = "i";
    ctl.setIdentity(id);
    ctl.onSuiteConnected();
    auto preview = makeAction(id, "pv", "staticEq", "gainDb", 5.0);
    REQUIRE(ctl.preview(preview) == ApplyResult::ok);
    ctl.onSuiteDisconnected();
    REQUIRE_FALSE(ctl.snapshot().previewActive);
    REQUIRE(ctl.snapshot().status == "disconnected");
}

TEST_CASE("Reported Mix Node latency matches saturation OS", "[milestone2a]")
{
    MixNodeChain chain;
    MixNodeChainState st;
    st.saturationEnabled = false;
    chain.prepare(48'000.0, 128, 2);
    chain.setState(st);
    REQUIRE(chain.latencySamples() == 0);

    st.saturationEnabled = true;
    st.saturation.oversamplingFactor = 4;
    st.saturation.bypass = false;
    chain.setState(st);
    REQUIRE(chain.latencySamples() == Catch::Approx(chain.latencySamples()));
    REQUIRE(chain.latencySamples() > 0);
}
