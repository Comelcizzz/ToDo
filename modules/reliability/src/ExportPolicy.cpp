#include "mastering/reliability/ExportPolicy.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mastering::reliability {

bool shouldApplyDither(ExportBitDepth depth) noexcept
{
    return depth == ExportBitDepth::pcm24;
}

DitherPolicy resolveDitherPolicy(ExportBitDepth depth, bool userRequested) noexcept
{
    if (depth == ExportBitDepth::float32)
        return DitherPolicy::none; // never dither 32-bit float
    if (!userRequested)
        return DitherPolicy::none;
    return DitherPolicy::tpdf;
}

std::string ditherPolicyDocumented() noexcept
{
    return "TPDF dither applies only when reducing to integer PCM (e.g. 24-bit). "
           "32-bit float exports never apply dither. Seed is documented for "
           "determinism when provided; otherwise implementation-defined.";
}

std::string serializeExportWorkflow(const ExportWorkflowState& state)
{
    nlohmann::json j;
    j["sourceMode"] = state.sourceMode;
    j["sampleRate"] = state.sampleRate;
    j["bitDepth"] = state.bitDepth == ExportBitDepth::float32 ? "float32" : "pcm24";
    j["ditherEnabled"] = state.ditherEnabled && shouldApplyDither(state.bitDepth);
    j["dither"] = state.dither == DitherPolicy::tpdf ? "tpdf" : "none";
    j["ditherSeed"] = state.ditherSeed;
    j["ditherPolicy"] = ditherPolicyDocumented();
    j["ceilingDbTp"] = state.ceilingDbTp;
    j["loudnessStatus"] = state.loudnessStatus;
    j["outputPath"] = state.outputPath;
    j["overwriteAllowed"] = state.overwriteAllowed;
    j["estimatedSizeBytes"] = state.estimatedSizeBytes;
    j["qcStatus"] = state.qcStatus;
    j["creativeLimiterAlreadyApplied"] = state.creativeLimiterAlreadyApplied;
    j["note"] = "Do not apply creative limiter twice";
    return j.dump(2);
}

bool validateBatchExportRouting(
    const BatchExportRoute& route,
    const std::vector<std::string>& availableRoles,
    const std::vector<std::string>& availableBusIds,
    std::string* error)
{
    if (!route.requireValidRouting)
        return true;
    for (const auto& role : route.includeRoles) {
        if (std::find(availableRoles.begin(), availableRoles.end(), role) == availableRoles.end()) {
            if (error)
                *error = "Missing role for batch export: " + role;
            return false;
        }
    }
    for (const auto& bus : route.includeBusIds) {
        if (std::find(availableBusIds.begin(), availableBusIds.end(), bus) == availableBusIds.end()) {
            if (error)
                *error = "Missing bus for batch export: " + bus;
            return false;
        }
    }
    return true;
}

} // namespace mastering::reliability
