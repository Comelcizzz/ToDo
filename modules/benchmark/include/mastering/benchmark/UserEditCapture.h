#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::benchmark {

struct UserEditEvent {
    std::string eventId;
    std::string sessionId;
    std::string projectId;
    std::string actionId;
    std::string changeType; // edit|reject|preview|apply|frequency|amount|section|target
    std::string originalProposalJson;
    std::string finalValueJson;
    std::string engineVersion;
    std::string profileId;
    std::string profileRevision;
    double evidenceScore {0.0};
    std::string timestamp;
    // Local structured event only — NOT training data until ML Lab + user consent.
};

[[nodiscard]] std::string serializeUserEditEvent(const UserEditEvent& event);
[[nodiscard]] UserEditEvent captureMixPassEdit(
    const project::MixPassAction& before,
    const project::MixPassAction& after,
    const std::string& changeType,
    const std::string& engineVersion,
    const std::string& profileId,
    const std::string& profileRevision);

class LocalUserEditLog {
public:
    void append(const UserEditEvent& event);
    [[nodiscard]] const std::vector<UserEditEvent>& events() const noexcept { return events_; }
    [[nodiscard]] std::string toJson() const;
    void clear();

private:
    std::vector<UserEditEvent> events_;
};

} // namespace mastering::benchmark
