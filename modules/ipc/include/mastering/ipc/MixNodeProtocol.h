#pragma once

#include "mastering/dsp/MixNodeChain.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::ipc {

inline constexpr int kMixNodeProtocolVersion = 1;
inline constexpr int kMixNodeIdentitySchemaVersion = 1;
inline constexpr int kActionSchemaVersion = 1;

// Analyzer track-analysis stays on BridgeProtocol schema v1.
// Mix Node messages use mixNodeProtocolVersion independently.

enum class MixNodeMessageType {
    Hello,
    RegisterInstance,
    Heartbeat,
    StateSnapshot,
    PreviewAction,
    CommitAction,
    CancelPreview,
    RequestState,
    StateUpdated,
    Error,
    Disconnect,
    Undo,
    Unknown
};

enum class MixNodeRolePreset {
    custom,
    rhythmGuitarLeft,
    rhythmGuitarRight,
    secondaryRhythmLeft,
    secondaryRhythmRight,
    leadGuitar,
    cleanGuitar,
    guitarBus,
    bass,
    bassBus,
    kick,
    snare,
    drumBus,
    leadVocal,
    screamVocal,
    vocalBus,
    synth,
    musicBus,
    master
};

enum class ChannelPosition {
    mono = 0,
    left,
    right,
    center,
    stereo
};

enum class ActionLifecycle {
    pending,
    previewing,
    applied,
    rejected,
    cancelled,
    superseded,
    error,
    stale
};

struct MixNodeIdentity {
    int schemaVersion {kMixNodeIdentitySchemaVersion};
    std::string projectId;
    std::string sessionId;
    std::string instanceId;
    std::string trackId;
    std::string trackName;
    std::string sourceType {"vst3"};
    MixNodeRolePreset role {MixNodeRolePreset::custom};
    std::string subRole;
    ChannelPosition channelPosition {ChannelPosition::stereo};
    std::string pairId;
    std::string layerId;
    std::string parentBusId;
};

struct MixNodeAction {
    int schemaVersion {kActionSchemaVersion};
    std::string actionId;
    std::uint32_t actionVersion {1};
    std::string projectId;
    std::string sessionId;
    std::string targetInstanceId;
    std::string targetTrackId;
    std::string processorId;  // inputGain | staticEq | dynamicEq | saturation | outputGain | bypass
    std::string parameterId;
    double previousValue {0.0};
    double proposedValue {0.0};
    double allowedMin {-120.0};
    double allowedMax {24.0};
    std::string sectionScope {"full"};
    bool preview {false};
    std::string origin {"suite"};
    double confidence {1.0};
    std::string explanation;
    std::int64_t createdAtMs {0};
    ActionLifecycle state {ActionLifecycle::pending};
    // Absolute chain snapshot for Apply/Undo (preferred over delta).
    std::optional<dsp::MixNodeChainState> previousChain;
    std::optional<dsp::MixNodeChainState> proposedChain;
};

struct MixNodeEnvelope {
    int mixNodeProtocolVersion {kMixNodeProtocolVersion};
    std::string projectId;
    std::string sessionId;
    std::string instanceId;
    std::string messageId;
    std::int64_t timestampMs {0};
    MixNodeMessageType type {MixNodeMessageType::Unknown};
    std::string payloadJson {"{}"};
    std::uint32_t checksum {0};
    bool requiresAck {false};
};

struct MixNodeProtocolError {
    std::string code;
    std::string message;
};

[[nodiscard]] std::string messageTypeToString(MixNodeMessageType type);
[[nodiscard]] MixNodeMessageType messageTypeFromString(std::string_view s);
[[nodiscard]] std::string rolePresetToString(MixNodeRolePreset role);
[[nodiscard]] std::optional<MixNodeRolePreset> rolePresetFromString(std::string_view s);
[[nodiscard]] std::string channelPositionToString(ChannelPosition p);
[[nodiscard]] std::optional<ChannelPosition> channelPositionFromString(std::string_view s);
[[nodiscard]] std::string actionLifecycleToString(ActionLifecycle s);
[[nodiscard]] std::optional<ActionLifecycle> actionLifecycleFromString(std::string_view s);

[[nodiscard]] std::uint32_t fnv1a32(std::string_view data) noexcept;
[[nodiscard]] std::string serializeEnvelope(const MixNodeEnvelope& env);
[[nodiscard]] std::optional<MixNodeEnvelope> parseEnvelope(std::string_view json, MixNodeProtocolError* error = nullptr);
[[nodiscard]] bool validateEnvelopeIds(
    const MixNodeEnvelope& env,
    std::string_view expectedProjectId,
    std::string_view expectedSessionId,
    std::string_view expectedInstanceId,
    MixNodeProtocolError* error = nullptr);

[[nodiscard]] std::string serializeIdentity(const MixNodeIdentity& id);
[[nodiscard]] std::optional<MixNodeIdentity> parseIdentity(std::string_view json);

[[nodiscard]] std::string serializeAction(const MixNodeAction& action);
[[nodiscard]] std::optional<MixNodeAction> parseAction(std::string_view json);

[[nodiscard]] std::string serializeChainState(const dsp::MixNodeChainState& state);
[[nodiscard]] std::optional<dsp::MixNodeChainState> parseChainState(std::string_view json);

// Host plugin state blob (committed only — never preview).
struct MixNodeHostState {
    int schemaVersion {dsp::kMixNodeChainSchemaVersion};
    MixNodeIdentity identity {};
    dsp::MixNodeChainState committed {};
    std::vector<std::string> committedActionIds {};
    std::uint32_t stateRevision {1};
};

[[nodiscard]] std::string serializeHostState(const MixNodeHostState& state);
[[nodiscard]] std::optional<MixNodeHostState> parseHostState(
    std::string_view json,
    MixNodeProtocolError* error = nullptr);

} // namespace mastering::ipc
