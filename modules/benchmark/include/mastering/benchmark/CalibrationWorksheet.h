#pragma once

#include <string>
#include <vector>

namespace mastering::benchmark {

// Explicit calibration notes for a profile/engine experiment.
// Profile changes require a new revision — never silent default mutation.
struct CalibrationWorksheet {
    std::string experimentId;
    std::string engineVersion;
    std::string profileId;
    std::string profileRevision;
    std::vector<std::string> observedFalsePositives;
    std::vector<std::string> missedProblems;
    std::vector<std::string> overprocessing;
    std::vector<std::string> underprocessing;
    std::vector<std::string> wrongTarget;
    std::vector<std::string> wrongSection;
    std::vector<std::string> wrongFrequency;
    std::vector<std::string> wrongActionType;
    std::vector<std::string> acceptedActions;
    std::vector<std::string> rejectedActions;
    std::vector<std::string> editedParameters;
    std::string listeningPreference;
    std::string notes;
    bool requiresNewProfileRevision {true};
};

[[nodiscard]] std::string serializeCalibrationWorksheet(const CalibrationWorksheet& sheet);
[[nodiscard]] CalibrationWorksheet emptyWorksheetFor(
    const std::string& experimentId,
    const std::string& engineVersion,
    const std::string& profileId,
    const std::string& profileRevision);

} // namespace mastering::benchmark
