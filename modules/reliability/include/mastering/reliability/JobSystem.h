#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

enum class JobType {
    stemValidation,
    streamingAnalysis,
    mixPassGeneration,
    referenceAnalysis,
    experiment,
    ablationRender,
    exportMaster,
    qc,
    blindPackage,
    renderRaw,
    renderAuto,
    renderCurrent,
    renderRef,
    renderStem,
    renderExcerpt
};

enum class JobStatus {
    queued,
    running,
    cancelling,
    cancelled,
    completed,
    failed,
    interrupted
};

struct JobRecord {
    std::string jobId;
    std::string projectId;
    JobType type {JobType::stemValidation};
    JobStatus status {JobStatus::queued};
    double progress {0.0}; // 0..1
    std::string currentStage;
    std::string error;
    std::string startTimeIso;
    std::string endTimeIso;
    bool resumable {false};
    std::vector<std::string> outputArtifacts;
    std::string snapshotJson; // immutable project snapshot for renders
    std::string renderGraphHash;
};

class JobSystem {
public:
    explicit JobSystem(int maxConcurrent = 2);

    [[nodiscard]] std::string enqueue(JobRecord job);
    bool cancel(std::string_view jobId);
    bool markInterrupted(std::string_view jobId);
    bool complete(std::string_view jobId, std::vector<std::string> artifacts);
    bool fail(std::string_view jobId, std::string error);
    bool setProgress(std::string_view jobId, double progress, std::string stage);
    bool startNext();

    [[nodiscard]] const std::vector<JobRecord>& jobs() const noexcept { return jobs_; }
    [[nodiscard]] std::optional<JobRecord> find(std::string_view jobId) const;
    [[nodiscard]] int runningCount() const noexcept;
    [[nodiscard]] int maxConcurrent() const noexcept { return maxConcurrent_; }
    [[nodiscard]] std::string toJson() const;

private:
    int maxConcurrent_ {2};
    std::vector<JobRecord> jobs_;
};

[[nodiscard]] std::string jobTypeToString(JobType t) noexcept;
[[nodiscard]] std::string jobStatusToString(JobStatus s) noexcept;

} // namespace mastering::reliability
