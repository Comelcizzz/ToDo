#include "mastering/benchmark/UserEditCapture.h"

#include <chrono>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {

std::string serializeUserEditEvent(const UserEditEvent& event)
{
    nlohmann::json j;
    j["eventId"] = event.eventId;
    j["sessionId"] = event.sessionId;
    j["projectId"] = event.projectId;
    j["actionId"] = event.actionId;
    j["changeType"] = event.changeType;
    j["originalProposalJson"] = event.originalProposalJson;
    j["finalValueJson"] = event.finalValueJson;
    j["engineVersion"] = event.engineVersion;
    j["profileId"] = event.profileId;
    j["profileRevision"] = event.profileRevision;
    j["evidenceScore"] = event.evidenceScore;
    j["timestamp"] = event.timestamp;
    j["notTrainingData"] = true;
    j["note"] = "Structured local event only; NOT ML training until explicit consent + ML Lab";
    return j.dump(2);
}

UserEditEvent captureMixPassEdit(
    const project::MixPassAction& before,
    const project::MixPassAction& after,
    const std::string& changeType,
    const std::string& engineVersion,
    const std::string& profileId,
    const std::string& profileRevision)
{
    UserEditEvent e;
    e.eventId = before.actionId + "-" + changeType;
    e.actionId = before.actionId;
    e.changeType = changeType;
    nlohmann::json orig {
        {"proposedValue", before.proposedValue},
        {"processorId", before.processorId},
        {"sectionScope", before.sectionScope},
        {"targetTrackId", before.targetTrackId}};
    nlohmann::json fin {
        {"proposedValue", after.proposedValue},
        {"processorId", after.processorId},
        {"sectionScope", after.sectionScope},
        {"targetTrackId", after.targetTrackId},
        {"state", after.state}};
    e.originalProposalJson = orig.dump();
    e.finalValueJson = fin.dump();
    e.engineVersion = engineVersion;
    e.profileId = profileId;
    e.profileRevision = profileRevision;
    e.evidenceScore = before.evidenceScore;
    e.timestamp = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    return e;
}

void LocalUserEditLog::append(const UserEditEvent& event)
{
    events_.push_back(event);
}

std::string LocalUserEditLog::toJson() const
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& e : events_)
        j.push_back(nlohmann::json::parse(serializeUserEditEvent(e)));
    return j.dump(2);
}

void LocalUserEditLog::clear()
{
    events_.clear();
}

} // namespace mastering::benchmark
