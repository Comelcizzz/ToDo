#include "mastering/reliability/JobSystem.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mastering::reliability {

std::string jobTypeToString(JobType t) noexcept
{
    switch (t) {
    case JobType::stemValidation: return "stem-validation";
    case JobType::streamingAnalysis: return "streaming-analysis";
    case JobType::mixPassGeneration: return "mix-pass-generation";
    case JobType::referenceAnalysis: return "reference-analysis";
    case JobType::experiment: return "experiment";
    case JobType::ablationRender: return "ablation-render";
    case JobType::exportMaster: return "export-master";
    case JobType::qc: return "qc";
    case JobType::blindPackage: return "blind-package";
    case JobType::renderRaw: return "render-raw";
    case JobType::renderAuto: return "render-auto";
    case JobType::renderCurrent: return "render-current";
    case JobType::renderRef: return "render-ref";
    case JobType::renderStem: return "render-stem";
    case JobType::renderExcerpt: return "render-excerpt";
    }
    return "unknown";
}

std::string jobStatusToString(JobStatus s) noexcept
{
    switch (s) {
    case JobStatus::queued: return "queued";
    case JobStatus::running: return "running";
    case JobStatus::cancelling: return "cancelling";
    case JobStatus::cancelled: return "cancelled";
    case JobStatus::completed: return "completed";
    case JobStatus::failed: return "failed";
    case JobStatus::interrupted: return "interrupted";
    }
    return "queued";
}

JobSystem::JobSystem(int maxConcurrent)
    : maxConcurrent_(std::max(1, maxConcurrent))
{
}

std::string JobSystem::enqueue(JobRecord job)
{
    if (job.jobId.empty())
        job.jobId = "job-" + std::to_string(jobs_.size() + 1);
    job.status = JobStatus::queued;
    jobs_.push_back(std::move(job));
    startNext();
    return jobs_.back().jobId;
}

int JobSystem::runningCount() const noexcept
{
    return static_cast<int>(std::count_if(jobs_.begin(), jobs_.end(), [](const JobRecord& j) {
        return j.status == JobStatus::running || j.status == JobStatus::cancelling;
    }));
}

bool JobSystem::startNext()
{
    while (runningCount() < maxConcurrent_) {
        auto it = std::find_if(jobs_.begin(), jobs_.end(), [](const JobRecord& j) {
            return j.status == JobStatus::queued;
        });
        if (it == jobs_.end())
            return false;
        it->status = JobStatus::running;
        it->currentStage = "started";
        it->progress = 0.01;
    }
    return true;
}

bool JobSystem::cancel(std::string_view jobId)
{
    for (auto& j : jobs_) {
        if (j.jobId != jobId)
            continue;
        if (j.status == JobStatus::queued) {
            j.status = JobStatus::cancelled;
            j.endTimeIso = "now";
            return true;
        }
        if (j.status == JobStatus::running) {
            j.status = JobStatus::cancelling;
            j.currentStage = "cancelling";
            return true;
        }
    }
    return false;
}

bool JobSystem::markInterrupted(std::string_view jobId)
{
    for (auto& j : jobs_) {
        if (j.jobId == jobId) {
            j.status = JobStatus::interrupted;
            j.currentStage = "interrupted";
            return true;
        }
    }
    return false;
}

bool JobSystem::complete(std::string_view jobId, std::vector<std::string> artifacts)
{
    for (auto& j : jobs_) {
        if (j.jobId != jobId)
            continue;
        if (j.status == JobStatus::cancelling) {
            j.status = JobStatus::cancelled;
            j.outputArtifacts.clear();
            return true;
        }
        j.status = JobStatus::completed;
        j.progress = 1.0;
        j.currentStage = "completed";
        j.outputArtifacts = std::move(artifacts);
        startNext();
        return true;
    }
    return false;
}

bool JobSystem::fail(std::string_view jobId, std::string error)
{
    for (auto& j : jobs_) {
        if (j.jobId != jobId)
            continue;
        j.status = JobStatus::failed;
        j.error = std::move(error);
        j.currentStage = "failed";
        j.outputArtifacts.clear();
        startNext();
        return true;
    }
    return false;
}

bool JobSystem::setProgress(std::string_view jobId, double progress, std::string stage)
{
    for (auto& j : jobs_) {
        if (j.jobId != jobId)
            continue;
        j.progress = std::clamp(progress, 0.0, 1.0);
        j.currentStage = std::move(stage);
        return true;
    }
    return false;
}

std::optional<JobRecord> JobSystem::find(std::string_view jobId) const
{
    for (const auto& j : jobs_) {
        if (j.jobId == jobId)
            return j;
    }
    return std::nullopt;
}

std::string JobSystem::toJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& j : jobs_) {
        arr.push_back({
            {"jobId", j.jobId},
            {"projectId", j.projectId},
            {"type", jobTypeToString(j.type)},
            {"status", jobStatusToString(j.status)},
            {"progress", j.progress},
            {"currentStage", j.currentStage},
            {"error", j.error},
            {"resumable", j.resumable},
            {"renderGraphHash", j.renderGraphHash},
            {"snapshotPinned", !j.snapshotJson.empty()},
            {"artifacts", j.outputArtifacts}});
    }
    return arr.dump(2);
}

} // namespace mastering::reliability
