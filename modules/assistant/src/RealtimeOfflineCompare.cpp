#include "mastering/assistant/RealtimeOfflineCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mastering::assistant {
namespace {

struct StereoBuf {
    std::vector<float> left;
    std::vector<float> right;
};

void processChain(
    StereoBuf& buf,
    double sampleRate,
    int blockSize,
    const SuiteCompareScenario& scenario)
{
    const int n = static_cast<int>(buf.left.size());
    dsp::ProcessorChain processor;
    processor.prepare(sampleRate, 2);
    dsp::ProcessorSettings settings;
    if (scenario.enableSectionSaturation)
        settings.saturation = 0.35;
    if (scenario.enableSectionCompressor) {
        settings.compressor.thresholdDb = -18.0;
        settings.compressor.ratio = 3.0;
    }
    if (scenario.enableSectionGain)
        settings.outputGainDb = scenario.sectionGainDb;
    processor.setSettings(settings);

    dsp::DynamicEqProcessor dynEq;
    dynEq.prepare(sampleRate, blockSize, 2);
    if (scenario.enableDynamicEq || scenario.enableSectionDynEq) {
        dsp::DynamicEqState st;
        st.bands[0].enabled = true;
        st.bands[0].frequencyHz = 250.0;
        st.bands[0].q = 1.2;
        st.bands[0].maxCutDb = 4.0 + scenario.sectionDynMaxCutDb;
        st.bands[0].thresholdDb = -24.0;
        dynEq.setState(st);
    }

    dsp::VocalRider rider;
    rider.prepare(sampleRate);
    rider.setTargetRmsDb(-18.0);

    dsp::ParallelCompressor parallel;
    parallel.prepare(sampleRate, blockSize, 2);
    {
        dsp::ParallelCompressorState ps;
        ps.wetAmount = scenario.parallelWet;
        ps.thresholdDb = -20.0;
        ps.ratio = 4.0;
        ps.bypass = !scenario.enableParallel;
        parallel.setState(ps);
    }

    dsp::StereoWidth width;
    width.prepare(sampleRate, blockSize);
    {
        dsp::StereoWidthState ws;
        ws.sideGainDb = scenario.sideGainDb;
        ws.width = 1.0;
        ws.lowBandMonoEnabled = scenario.lowBandMonoEnabled;
        ws.minCorrelation = -1.0;
        ws.bypass = !scenario.enableStereoWidth;
        width.setState(ws);
    }

    dsp::MasterSafetyChain safety;
    safety.prepare(sampleRate, blockSize, 2, 4);
    {
        dsp::MasterSafetySettings s;
        s.limiterEnabled = scenario.enableMasterSafety;
        s.limiter.ceilingDbTp = -1.0;
        s.limiter.lookAheadMs = 1.5;
        s.limiter.oversamplingFactor = 4;
        safety.setSettings(s);
    }

    for (int offset = 0; offset < n; offset += blockSize) {
        const int m = std::min(blockSize, n - offset);
        float* ch[2] {buf.left.data() + offset, buf.right.data() + offset};
        processor.process(ch, 2, m);
        if (scenario.enableVocalRider)
            rider.process(ch, 2, m);
        if (scenario.enableDynamicEq || scenario.enableSectionDynEq)
            dynEq.process(ch, nullptr, 2, m);
        if (scenario.enableParallel)
            parallel.process(ch, 2, m);
        if (scenario.enableStereoWidth)
            width.process(ch, 2, m);
        if (scenario.enableMasterSafety)
            safety.process(ch, 2, m);
    }
}

double rmsDb(const StereoBuf& b)
{
    double s = 0.0;
    const auto n = b.left.size();
    for (std::size_t i = 0; i < n; ++i) {
        s += double(b.left[i]) * double(b.left[i]);
        s += double(b.right[i]) * double(b.right[i]);
    }
    const double rms = n == 0 ? 0.0 : std::sqrt(s / double(n * 2));
    return 20.0 * std::log10(std::max(1.0e-12, rms));
}

double peakDb(const StereoBuf& b)
{
    double p = 0.0;
    for (std::size_t i = 0; i < b.left.size(); ++i) {
        p = std::max(p, std::abs(double(b.left[i])));
        p = std::max(p, std::abs(double(b.right[i])));
    }
    return 20.0 * std::log10(std::max(1.0e-12, p));
}

double nullRejectionDb(const StereoBuf& a, const StereoBuf& b)
{
    double diff = 0.0;
    double ref = 0.0;
    for (std::size_t i = 0; i < a.left.size(); ++i) {
        const double dl = double(a.left[i]) - double(b.left[i]);
        const double dr = double(a.right[i]) - double(b.right[i]);
        diff += dl * dl + dr * dr;
        ref += double(a.left[i]) * double(a.left[i]) + double(a.right[i]) * double(a.right[i]);
    }
    if (ref < 1.0e-24)
        return 120.0;
    return 10.0 * std::log10(ref / std::max(1.0e-24, diff));
}

StereoBuf makeTone(int n, double sr, double hz)
{
    StereoBuf b;
    b.left.resize(static_cast<std::size_t>(n));
    b.right.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / sr;
        b.left[static_cast<std::size_t>(i)] = float(0.2 * std::sin(2.0 * std::numbers::pi * hz * t));
        b.right[static_cast<std::size_t>(i)] = float(0.2 * std::sin(2.0 * std::numbers::pi * hz * t + 0.3));
    }
    return b;
}

} // namespace

SuiteCompareResult compareRealtimeOffline(
    const std::vector<float>& left,
    const std::vector<float>& right,
    double sampleRate,
    const SuiteCompareScenario& scenario,
    int realtimeBlockSize,
    SuiteCompareTolerances tolerances)
{
    SuiteCompareResult result;
    result.scenario = scenario.name;

    StereoBuf rt {left, right};
    StereoBuf offline {left, right};

    // Realtime-style: small blocks.
    processChain(rt, sampleRate, realtimeBlockSize, scenario);
    // Offline-style: larger blocks (same DSP, different block size — StemEngine uses 2048 offline).
    processChain(offline, sampleRate, 2048, scenario);

    // Compensate documented MasterSafety look-ahead latency if enabled.
    int latency = 0;
    if (scenario.enableMasterSafety) {
        dsp::MasterSafetyChain probe;
        probe.prepare(sampleRate, 512, 2, 4);
        dsp::MasterSafetySettings s;
        s.limiterEnabled = true;
        s.limiter.lookAheadMs = 1.5;
        s.limiter.oversamplingFactor = 4;
        probe.setSettings(s);
        latency = probe.latencySamples();
    }
    if (latency > 0 && static_cast<int>(rt.left.size()) > latency) {
        // Both paths use same safety chain → same latency; compare aligned tails.
        const auto n = rt.left.size() - static_cast<std::size_t>(latency);
        StereoBuf rtTrim;
        StereoBuf offTrim;
        rtTrim.left.assign(rt.left.begin() + latency, rt.left.begin() + latency + static_cast<std::ptrdiff_t>(n));
        rtTrim.right.assign(rt.right.begin() + latency, rt.right.begin() + latency + static_cast<std::ptrdiff_t>(n));
        offTrim.left.assign(offline.left.begin() + latency, offline.left.begin() + latency + static_cast<std::ptrdiff_t>(n));
        offTrim.right.assign(offline.right.begin() + latency, offline.right.begin() + latency + static_cast<std::ptrdiff_t>(n));
        rt = std::move(rtTrim);
        offline = std::move(offTrim);
    }

    result.rmsDeltaDb = std::abs(rmsDb(rt) - rmsDb(offline));
    result.peakDeltaDb = std::abs(peakDb(rt) - peakDb(offline));
    result.nullRejectionDb = nullRejectionDb(rt, offline);
    result.passed = result.rmsDeltaDb <= tolerances.maxRmsDeltaDb
        && result.peakDeltaDb <= tolerances.maxPeakDeltaDb
        && result.nullRejectionDb >= tolerances.minNullRejectionDb;
    result.notes = result.passed ? "pass" : "fail";
    return result;
}

std::vector<SuiteCompareResult> runSuiteConsistencyBattery(double sampleRate, double durationSeconds)
{
    const int n = static_cast<int>(sampleRate * durationSeconds);
    const auto tone = makeTone(n, sampleRate, 440.0);

    std::vector<SuiteCompareScenario> scenarios;
    scenarios.push_back({"Dynamic EQ", true, false, false, false, false, false, false, false, false});
    scenarios.push_back({"Vocal Rider", false, true, false, false, false, false, false, false, false});
    scenarios.push_back({"section gain", false, false, true, false, false, false, false, false, false});
    scenarios.back().sectionGainDb = 1.5;
    scenarios.push_back({"section DynEQ offset", false, false, false, true, false, false, false, false, false});
    scenarios.back().sectionDynMaxCutDb = 2.0;
    scenarios.push_back({"compressor section offset", false, false, false, false, true, false, false, false, false});
    scenarios.push_back({"saturation section offset", false, false, false, false, false, true, false, false, false});
    scenarios.push_back({"virtual Drum Bus / parallel", false, false, false, false, false, false, true, false, false});
    scenarios.back().parallelWet = 0.6;
    scenarios.push_back({"StereoWidth", false, false, false, false, false, false, false, true, false});
    scenarios.back().sideGainDb = -3.0;
    scenarios.push_back({"master safety", false, false, false, false, false, false, false, false, true});
    SuiteCompareScenario full {"full AUTO graph", true, true, true, true, true, false, true, true, true};
    full.sectionGainDb = 0.5;
    full.parallelWet = 0.4;
    full.sideGainDb = -2.0;
    scenarios.push_back(full);

    // Non-linear processors (comp/sat/safety/rider) may differ slightly across block sizes;
    // use slightly looser null for those scenarios.
    SuiteCompareTolerances tight;
    SuiteCompareTolerances loose;
    loose.maxRmsDeltaDb = 0.35;
    loose.maxPeakDeltaDb = 0.5;
    loose.minNullRejectionDb = 35.0;

    std::vector<SuiteCompareResult> results;
    for (const auto& sc : scenarios) {
        const bool nonlinear = sc.enableVocalRider || sc.enableSectionCompressor
            || sc.enableSectionSaturation || sc.enableParallel || sc.enableMasterSafety;
        results.push_back(compareRealtimeOffline(
            tone.left, tone.right, sampleRate, sc, 512, nonlinear ? loose : tight));
    }
    return results;
}

} // namespace mastering::assistant
