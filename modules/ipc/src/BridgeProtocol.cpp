#include "mastering/ipc/BridgeProtocol.h"

#include <nlohmann/json.hpp>

namespace mastering::ipc {

ValidationResult validateTrackAnalysisPayload(std::string_view json)
{
    try {
        const auto value = nlohmann::json::parse(json);
        if (!value.is_object())
            return {false, "IPC payload must be a JSON object"};

        const auto schema = value.value("schemaVersion", 0);
        if (schema < 1)
            return {false, "IPC payload missing schemaVersion"};
        if (schema > kCurrentSchemaVersion) {
            return {
                false,
                "Unsupported IPC schemaVersion "
                    + std::to_string(schema)
                    + " (max supported is "
                    + std::to_string(kCurrentSchemaVersion)
                    + ")"
            };
        }

        if (value.value("type", "") != "track-analysis")
            return {false, "IPC type must be track-analysis"};
        if (!value.contains("role") || !value["role"].is_string())
            return {false, "IPC payload missing role"};
        if (!value.contains("metrics") || !value["metrics"].is_object())
            return {false, "IPC payload missing metrics object"};

        const auto& metrics = value["metrics"];
        if (metrics.contains("integratedLufs")
            && metrics.value("integratedLufsIsValid", false) != true) {
            return {false, "IPC metrics claim integratedLufs without integratedLufsIsValid"};
        }
        if (metrics.contains("estimatedTruePeakDbtp")
            && metrics.value("truePeakIsEstimate", false) != true) {
            return {false, "IPC metrics claim estimatedTruePeakDbtp without truePeakIsEstimate"};
        }

        return {true, {}};
    } catch (const nlohmann::json::exception& error) {
        return {false, std::string("Malformed IPC JSON: ") + error.what()};
    }
}

} // namespace mastering::ipc
