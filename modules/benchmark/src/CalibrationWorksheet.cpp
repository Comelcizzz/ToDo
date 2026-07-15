#include "mastering/benchmark/CalibrationWorksheet.h"

#include <nlohmann/json.hpp>

namespace mastering::benchmark {

namespace {

nlohmann::json toArray(const std::vector<std::string>& values)
{
    return nlohmann::json(values);
}

} // namespace

CalibrationWorksheet emptyWorksheetFor(
    const std::string& experimentId,
    const std::string& engineVersion,
    const std::string& profileId,
    const std::string& profileRevision)
{
    CalibrationWorksheet sheet;
    sheet.experimentId = experimentId;
    sheet.engineVersion = engineVersion;
    sheet.profileId = profileId;
    sheet.profileRevision = profileRevision;
    sheet.notes =
        "Fill after listening/objective review. Changing defaults requires a new profile revision.";
    return sheet;
}

std::string serializeCalibrationWorksheet(const CalibrationWorksheet& sheet)
{
    nlohmann::json j;
    j["experimentId"] = sheet.experimentId;
    j["engineVersion"] = sheet.engineVersion;
    j["profileId"] = sheet.profileId;
    j["profileRevision"] = sheet.profileRevision;
    j["observedFalsePositives"] = toArray(sheet.observedFalsePositives);
    j["missedProblems"] = toArray(sheet.missedProblems);
    j["overprocessing"] = toArray(sheet.overprocessing);
    j["underprocessing"] = toArray(sheet.underprocessing);
    j["wrongTarget"] = toArray(sheet.wrongTarget);
    j["wrongSection"] = toArray(sheet.wrongSection);
    j["wrongFrequency"] = toArray(sheet.wrongFrequency);
    j["wrongActionType"] = toArray(sheet.wrongActionType);
    j["acceptedActions"] = toArray(sheet.acceptedActions);
    j["rejectedActions"] = toArray(sheet.rejectedActions);
    j["editedParameters"] = toArray(sheet.editedParameters);
    j["listeningPreference"] = sheet.listeningPreference;
    j["notes"] = sheet.notes;
    j["requiresNewProfileRevision"] = sheet.requiresNewProfileRevision;
    j["policy"] = "Never silently mutate default profiles";
    return j.dump(2);
}

} // namespace mastering::benchmark
