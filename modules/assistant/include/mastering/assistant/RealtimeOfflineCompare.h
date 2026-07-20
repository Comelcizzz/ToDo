#pragma once

#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/MasterSafetyChain.h"
#include "mastering/dsp/ParallelCompressor.h"
#include "mastering/dsp/ProcessorChain.h"
#include "mastering/dsp/StereoWidth.h"
#include "mastering/dsp/VocalRider.h"

#include <string>
#include <vector>

namespace mastering::assistant {

struct SuiteCompareScenario {
    std::string name;
    bool enableDynamicEq {false};
    bool enableVocalRider {false};
    bool enableSectionGain {false};
    bool enableSectionDynEq {false};
    bool enableSectionCompressor {false};
    bool enableSectionSaturation {false};
    bool enableParallel {false};
    bool enableStereoWidth {false};
    bool enableMasterSafety {true};
    double sectionGainDb {0.0};
    double sectionDynMaxCutDb {0.0};
    double parallelWet {0.5};
    double sideGainDb {0.0};
    bool lowBandMonoEnabled {true};
};

struct SuiteCompareResult {
    std::string scenario;
    double rmsDeltaDb {0.0};
    double peakDeltaDb {0.0};
    double nullRejectionDb {0.0};
    bool passed {false};
    std::string notes;
};

struct SuiteCompareTolerances {
    double maxRmsDeltaDb {0.05};
    double maxPeakDeltaDb {0.1};
    double minNullRejectionDb {60.0}; // higher = closer match
};

// Suite-level realtime (block) vs offline (single-pass same blocks, separate instance)
// comparison using the same DSP processors as StemEngine — without FL Studio / JUCE IO.
[[nodiscard]] SuiteCompareResult compareRealtimeOffline(
    const std::vector<float>& left,
    const std::vector<float>& right,
    double sampleRate,
    const SuiteCompareScenario& scenario,
    int realtimeBlockSize = 512,
    SuiteCompareTolerances tolerances = {});

[[nodiscard]] std::vector<SuiteCompareResult> runSuiteConsistencyBattery(
    double sampleRate = 48'000.0,
    double durationSeconds = 2.0);

} // namespace mastering::assistant
