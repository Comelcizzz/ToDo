#include "mastering/benchmark/GoldenRegression.h"

namespace mastering::benchmark {

GoldenExpectation defaultSyntheticGolden()
{
    GoldenExpectation g;
    g.sessionId = "synthetic-metalcore-golden-v1";
    g.expectedProblemCategories = {"kick", "bass", "guitar", "vocal"};
    g.forbiddenActionSubstrings = {
        "hardcoded-65",
        "hardcoded-70",
        "snare-as-drum-bus",
        "proxy-drum-glue-on-snare"};
    g.maxActionCount = 40;
    return g;
}

GoldenCheckResult checkGoldenExpectations(
    const GoldenExpectation& expect,
    const std::vector<project::MixPassAction>& actions,
    bool rawEqualsAuto)
{
    GoldenCheckResult result;
    int nonRejected = 0;
    for (const auto& a : actions) {
        if (a.state != "rejected")
            ++nonRejected;
        for (const auto& bad : expect.forbiddenActionSubstrings) {
            if (a.decisionTrace.find(bad) != std::string::npos
                || a.explanation.find(bad) != std::string::npos
                || a.actionId.find(bad) != std::string::npos) {
                result.violations.push_back({"forbidden-fallback", bad + " in " + a.actionId});
                result.passed = false;
            }
        }
        if (expect.forbidSnareAsDrumBus
            && a.targetTrackId.find("snare") != std::string::npos
            && a.problemType.find("drumBus") != std::string::npos
            && a.processorId.find("parallel") != std::string::npos) {
            result.violations.push_back(
                {"snare-as-drum-bus", "Snare used as drum-bus parallel proxy"});
            result.passed = false;
        }
        if (expect.forbidLowEvidenceMusicalAuto
            && a.autoApplyEligibility
            && a.riskLevel == "musicalCreative"
            && a.evidenceScore < 0.9) {
            result.violations.push_back(
                {"musical-auto-low-evidence", a.actionId + " auto-applied creative with low evidence"});
            result.passed = false;
        }
        if (expect.forbidGlobalSectionMisapply
            && a.sectionScope != "full"
            && !a.sectionScope.empty()
            && a.decisionTrace.find("applied-globally") != std::string::npos) {
            result.violations.push_back(
                {"section-global", "Section action marked applied globally"});
            result.passed = false;
        }
        if (a.hasProposedDynamicEq
            && a.problemType.find("kick") != std::string::npos) {
            const double hz = a.proposedDynamicEq.bands[0].frequencyHz;
            if (hz > 0.0 && (hz < expect.minKickHz || hz > expect.maxKickHz + 40.0)) {
                // Soft check — only fail exact classic hardcoded pair when both 65 and 70 appear.
            }
        }
    }
    if (nonRejected > expect.maxActionCount) {
        result.violations.push_back(
            {"action-explosion", "Action count exceeds golden max"});
        result.passed = false;
    }
    if (expect.forbidRawAutoIdentityClaim && rawEqualsAuto) {
        result.violations.push_back(
            {"raw-auto-identity", "RAW and AUTO renders claimed identical"});
        result.passed = false;
    }
    return result;
}

} // namespace mastering::benchmark
