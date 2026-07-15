#include "mastering/ipc/MixNodeProtocol.h"

#include <nlohmann/json.hpp>
#include <chrono>

namespace mastering::ipc {
namespace {

nlohmann::json chainToJson(const dsp::MixNodeChainState& state)
{
    dsp::MixNodeChain tmp;
    tmp.setState(state);
    return nlohmann::json::parse(tmp.toJson());
}

std::optional<dsp::MixNodeChainState> chainFromJson(const nlohmann::json& j)
{
    dsp::MixNodeChain tmp;
    if (!tmp.fromJson(j.dump()))
        return std::nullopt;
    return tmp.state();
}

} // namespace

std::string messageTypeToString(MixNodeMessageType type)
{
    switch (type) {
    case MixNodeMessageType::Hello: return "Hello";
    case MixNodeMessageType::RegisterInstance: return "RegisterInstance";
    case MixNodeMessageType::Heartbeat: return "Heartbeat";
    case MixNodeMessageType::StateSnapshot: return "StateSnapshot";
    case MixNodeMessageType::PreviewAction: return "PreviewAction";
    case MixNodeMessageType::CommitAction: return "CommitAction";
    case MixNodeMessageType::CancelPreview: return "CancelPreview";
    case MixNodeMessageType::RequestState: return "RequestState";
    case MixNodeMessageType::StateUpdated: return "StateUpdated";
    case MixNodeMessageType::Error: return "Error";
    case MixNodeMessageType::Disconnect: return "Disconnect";
    case MixNodeMessageType::Undo: return "Undo";
    default: return "Unknown";
    }
}

MixNodeMessageType messageTypeFromString(std::string_view s)
{
    if (s == "Hello") return MixNodeMessageType::Hello;
    if (s == "RegisterInstance") return MixNodeMessageType::RegisterInstance;
    if (s == "Heartbeat") return MixNodeMessageType::Heartbeat;
    if (s == "StateSnapshot") return MixNodeMessageType::StateSnapshot;
    if (s == "PreviewAction") return MixNodeMessageType::PreviewAction;
    if (s == "CommitAction") return MixNodeMessageType::CommitAction;
    if (s == "CancelPreview") return MixNodeMessageType::CancelPreview;
    if (s == "RequestState") return MixNodeMessageType::RequestState;
    if (s == "StateUpdated") return MixNodeMessageType::StateUpdated;
    if (s == "Error") return MixNodeMessageType::Error;
    if (s == "Disconnect") return MixNodeMessageType::Disconnect;
    if (s == "Undo") return MixNodeMessageType::Undo;
    return MixNodeMessageType::Unknown;
}

std::string rolePresetToString(MixNodeRolePreset role)
{
    switch (role) {
    case MixNodeRolePreset::rhythmGuitarLeft: return "Rhythm Guitar Left";
    case MixNodeRolePreset::rhythmGuitarRight: return "Rhythm Guitar Right";
    case MixNodeRolePreset::secondaryRhythmLeft: return "Secondary Rhythm Left";
    case MixNodeRolePreset::secondaryRhythmRight: return "Secondary Rhythm Right";
    case MixNodeRolePreset::leadGuitar: return "Lead Guitar";
    case MixNodeRolePreset::cleanGuitar: return "Clean Guitar";
    case MixNodeRolePreset::guitarBus: return "Guitar Bus";
    case MixNodeRolePreset::bass: return "Bass";
    case MixNodeRolePreset::bassBus: return "Bass Bus";
    case MixNodeRolePreset::kick: return "Kick";
    case MixNodeRolePreset::snare: return "Snare";
    case MixNodeRolePreset::drumBus: return "Drum Bus";
    case MixNodeRolePreset::leadVocal: return "Lead Vocal";
    case MixNodeRolePreset::screamVocal: return "Scream Vocal";
    case MixNodeRolePreset::vocalBus: return "Vocal Bus";
    case MixNodeRolePreset::synth: return "Synth";
    case MixNodeRolePreset::musicBus: return "Music Bus";
    case MixNodeRolePreset::master: return "Master";
    case MixNodeRolePreset::custom:
    default: return "Custom";
    }
}

std::optional<MixNodeRolePreset> rolePresetFromString(std::string_view s)
{
    if (s == "Rhythm Guitar Left") return MixNodeRolePreset::rhythmGuitarLeft;
    if (s == "Rhythm Guitar Right") return MixNodeRolePreset::rhythmGuitarRight;
    if (s == "Secondary Rhythm Left") return MixNodeRolePreset::secondaryRhythmLeft;
    if (s == "Secondary Rhythm Right") return MixNodeRolePreset::secondaryRhythmRight;
    if (s == "Lead Guitar") return MixNodeRolePreset::leadGuitar;
    if (s == "Clean Guitar") return MixNodeRolePreset::cleanGuitar;
    if (s == "Guitar Bus") return MixNodeRolePreset::guitarBus;
    if (s == "Bass") return MixNodeRolePreset::bass;
    if (s == "Bass Bus") return MixNodeRolePreset::bassBus;
    if (s == "Kick") return MixNodeRolePreset::kick;
    if (s == "Snare") return MixNodeRolePreset::snare;
    if (s == "Drum Bus") return MixNodeRolePreset::drumBus;
    if (s == "Lead Vocal") return MixNodeRolePreset::leadVocal;
    if (s == "Scream Vocal") return MixNodeRolePreset::screamVocal;
    if (s == "Vocal Bus") return MixNodeRolePreset::vocalBus;
    if (s == "Synth") return MixNodeRolePreset::synth;
    if (s == "Music Bus") return MixNodeRolePreset::musicBus;
    if (s == "Master") return MixNodeRolePreset::master;
    if (s == "Custom" || s == "custom") return MixNodeRolePreset::custom;
    return std::nullopt;
}

std::string channelPositionToString(ChannelPosition p)
{
    switch (p) {
    case ChannelPosition::mono: return "Mono";
    case ChannelPosition::left: return "L";
    case ChannelPosition::right: return "R";
    case ChannelPosition::center: return "C";
    case ChannelPosition::stereo:
    default: return "Stereo";
    }
}

std::optional<ChannelPosition> channelPositionFromString(std::string_view s)
{
    if (s == "Mono" || s == "mono") return ChannelPosition::mono;
    if (s == "L" || s == "Left" || s == "left") return ChannelPosition::left;
    if (s == "R" || s == "Right" || s == "right") return ChannelPosition::right;
    if (s == "C" || s == "Center" || s == "center") return ChannelPosition::center;
    if (s == "Stereo" || s == "stereo") return ChannelPosition::stereo;
    return std::nullopt;
}

std::string actionLifecycleToString(ActionLifecycle s)
{
    switch (s) {
    case ActionLifecycle::pending: return "pending";
    case ActionLifecycle::previewing: return "previewing";
    case ActionLifecycle::applied: return "applied";
    case ActionLifecycle::rejected: return "rejected";
    case ActionLifecycle::cancelled: return "cancelled";
    case ActionLifecycle::superseded: return "superseded";
    case ActionLifecycle::error: return "error";
    case ActionLifecycle::stale: return "stale";
    }
    return "error";
}

std::optional<ActionLifecycle> actionLifecycleFromString(std::string_view s)
{
    if (s == "pending") return ActionLifecycle::pending;
    if (s == "previewing") return ActionLifecycle::previewing;
    if (s == "applied") return ActionLifecycle::applied;
    if (s == "rejected") return ActionLifecycle::rejected;
    if (s == "cancelled") return ActionLifecycle::cancelled;
    if (s == "superseded") return ActionLifecycle::superseded;
    if (s == "error") return ActionLifecycle::error;
    if (s == "stale") return ActionLifecycle::stale;
    return std::nullopt;
}

std::uint32_t fnv1a32(std::string_view data) noexcept
{
    std::uint32_t h = 2166136261u;
    for (unsigned char c : data) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::string serializeEnvelope(const MixNodeEnvelope& env)
{
    nlohmann::json j;
    j["mixNodeProtocolVersion"] = env.mixNodeProtocolVersion;
    j["projectId"] = env.projectId;
    j["sessionId"] = env.sessionId;
    j["instanceId"] = env.instanceId;
    j["messageId"] = env.messageId;
    j["timestampMs"] = env.timestampMs;
    j["type"] = messageTypeToString(env.type);
    j["payload"] = nlohmann::json::parse(env.payloadJson.empty() ? "{}" : env.payloadJson, nullptr, false);
    if (j["payload"].is_discarded())
        j["payload"] = nlohmann::json::object();
    j["requiresAck"] = env.requiresAck;
    const auto body = j.dump();
    j["checksum"] = fnv1a32(body);
    return j.dump();
}

std::optional<MixNodeEnvelope> parseEnvelope(std::string_view json, MixNodeProtocolError* error)
{
    try {
        const auto j = nlohmann::json::parse(json);
        MixNodeEnvelope env;
        env.mixNodeProtocolVersion = j.value("mixNodeProtocolVersion", 0);
        if (env.mixNodeProtocolVersion < 1) {
            if (error) *error = {"schema", "missing mixNodeProtocolVersion"};
            return std::nullopt;
        }
        if (env.mixNodeProtocolVersion > kMixNodeProtocolVersion) {
            if (error) *error = {"schema", "unsupported mixNodeProtocolVersion"};
            return std::nullopt;
        }
        env.projectId = j.value("projectId", "");
        env.sessionId = j.value("sessionId", "");
        env.instanceId = j.value("instanceId", "");
        env.messageId = j.value("messageId", "");
        env.timestampMs = j.value("timestampMs", 0);
        env.type = messageTypeFromString(j.value("type", ""));
        if (env.type == MixNodeMessageType::Unknown) {
            if (error) *error = {"type", "unknown message type"};
            return std::nullopt;
        }
        if (j.contains("payload"))
            env.payloadJson = j["payload"].dump();
        env.requiresAck = j.value("requiresAck", false);
        env.checksum = j.value("checksum", 0u);
        return env;
    } catch (const std::exception& ex) {
        if (error) *error = {"json", ex.what()};
        return std::nullopt;
    }
}

bool validateEnvelopeIds(
    const MixNodeEnvelope& env,
    std::string_view expectedProjectId,
    std::string_view expectedSessionId,
    std::string_view expectedInstanceId,
    MixNodeProtocolError* error)
{
    if (!expectedProjectId.empty() && env.projectId != expectedProjectId) {
        if (error) *error = {"projectId", "projectId mismatch"};
        return false;
    }
    if (!expectedSessionId.empty() && env.sessionId != expectedSessionId) {
        if (error) *error = {"sessionId", "sessionId mismatch"};
        return false;
    }
    if (!expectedInstanceId.empty() && env.instanceId != expectedInstanceId) {
        if (error) *error = {"instanceId", "instanceId mismatch"};
        return false;
    }
    return true;
}

std::string serializeIdentity(const MixNodeIdentity& id)
{
    nlohmann::json j;
    j["schemaVersion"] = id.schemaVersion;
    j["projectId"] = id.projectId;
    j["sessionId"] = id.sessionId;
    j["instanceId"] = id.instanceId;
    j["trackId"] = id.trackId;
    j["trackName"] = id.trackName;
    j["sourceType"] = id.sourceType;
    j["role"] = rolePresetToString(id.role);
    j["subRole"] = id.subRole;
    j["channelPosition"] = channelPositionToString(id.channelPosition);
    j["pairId"] = id.pairId;
    j["layerId"] = id.layerId;
    j["parentBusId"] = id.parentBusId;
    return j.dump();
}

std::optional<MixNodeIdentity> parseIdentity(std::string_view json)
{
    try {
        const auto j = nlohmann::json::parse(json);
        MixNodeIdentity id;
        id.schemaVersion = j.value("schemaVersion", kMixNodeIdentitySchemaVersion);
        id.projectId = j.value("projectId", "");
        id.sessionId = j.value("sessionId", "");
        id.instanceId = j.value("instanceId", "");
        id.trackId = j.value("trackId", "");
        id.trackName = j.value("trackName", "");
        id.sourceType = j.value("sourceType", "vst3");
        if (const auto role = rolePresetFromString(j.value("role", "Custom")))
            id.role = *role;
        id.subRole = j.value("subRole", "");
        if (const auto pos = channelPositionFromString(j.value("channelPosition", "Stereo")))
            id.channelPosition = *pos;
        id.pairId = j.value("pairId", "");
        id.layerId = j.value("layerId", "");
        id.parentBusId = j.value("parentBusId", "");
        return id;
    } catch (...) {
        return std::nullopt;
    }
}

std::string serializeAction(const MixNodeAction& action)
{
    nlohmann::json j;
    j["schemaVersion"] = action.schemaVersion;
    j["actionId"] = action.actionId;
    j["actionVersion"] = action.actionVersion;
    j["projectId"] = action.projectId;
    j["sessionId"] = action.sessionId;
    j["targetInstanceId"] = action.targetInstanceId;
    j["targetTrackId"] = action.targetTrackId;
    j["processorId"] = action.processorId;
    j["parameterId"] = action.parameterId;
    j["previousValue"] = action.previousValue;
    j["proposedValue"] = action.proposedValue;
    j["allowedRange"] = {action.allowedMin, action.allowedMax};
    j["sectionScope"] = action.sectionScope;
    j["preview"] = action.preview;
    j["origin"] = action.origin;
    j["confidence"] = action.confidence;
    j["explanation"] = action.explanation;
    j["createdAt"] = action.createdAtMs;
    j["state"] = actionLifecycleToString(action.state);
    if (action.previousChain)
        j["previousChain"] = chainToJson(*action.previousChain);
    if (action.proposedChain)
        j["proposedChain"] = chainToJson(*action.proposedChain);
    return j.dump();
}

std::optional<MixNodeAction> parseAction(std::string_view json)
{
    try {
        const auto j = nlohmann::json::parse(json);
        MixNodeAction a;
        a.schemaVersion = j.value("schemaVersion", kActionSchemaVersion);
        a.actionId = j.value("actionId", "");
        a.actionVersion = j.value("actionVersion", 1u);
        a.projectId = j.value("projectId", "");
        a.sessionId = j.value("sessionId", "");
        a.targetInstanceId = j.value("targetInstanceId", "");
        a.targetTrackId = j.value("targetTrackId", "");
        a.processorId = j.value("processorId", "");
        a.parameterId = j.value("parameterId", "");
        a.previousValue = j.value("previousValue", 0.0);
        a.proposedValue = j.value("proposedValue", 0.0);
        if (j.contains("allowedRange") && j["allowedRange"].is_array() && j["allowedRange"].size() >= 2) {
            a.allowedMin = j["allowedRange"][0].get<double>();
            a.allowedMax = j["allowedRange"][1].get<double>();
        }
        a.sectionScope = j.value("sectionScope", "full");
        a.preview = j.value("preview", false);
        a.origin = j.value("origin", "suite");
        a.confidence = j.value("confidence", 1.0);
        a.explanation = j.value("explanation", "");
        a.createdAtMs = j.value("createdAt", 0);
        if (const auto st = actionLifecycleFromString(j.value("state", "pending")))
            a.state = *st;
        if (j.contains("previousChain"))
            a.previousChain = chainFromJson(j["previousChain"]);
        if (j.contains("proposedChain"))
            a.proposedChain = chainFromJson(j["proposedChain"]);
        return a;
    } catch (...) {
        return std::nullopt;
    }
}

std::string serializeChainState(const dsp::MixNodeChainState& state)
{
    return chainToJson(state).dump();
}

std::optional<dsp::MixNodeChainState> parseChainState(std::string_view json)
{
    try {
        return chainFromJson(nlohmann::json::parse(json));
    } catch (...) {
        return std::nullopt;
    }
}

std::string serializeHostState(const MixNodeHostState& state)
{
    nlohmann::json j;
    j["schemaVersion"] = state.schemaVersion;
    j["identity"] = nlohmann::json::parse(serializeIdentity(state.identity));
    j["committed"] = chainToJson(state.committed);
    j["committedActionIds"] = state.committedActionIds;
    j["stateRevision"] = state.stateRevision;
    // Explicit: preview must never be serialized here.
    j["hasPreview"] = false;
    return j.dump();
}

std::optional<MixNodeHostState> parseHostState(std::string_view json, MixNodeProtocolError* error)
{
    try {
        const auto j = nlohmann::json::parse(json);
        MixNodeHostState state;
        state.schemaVersion = j.value("schemaVersion", dsp::kMixNodeChainSchemaVersion);
        if (state.schemaVersion < 1) {
            if (error) *error = {"schema", "missing schemaVersion"};
            return std::nullopt;
        }
        if (state.schemaVersion > dsp::kMixNodeChainSchemaVersion) {
            if (error) *error = {"schema", "unsupported newer schema"};
            return std::nullopt;
        }
        if (j.contains("identity")) {
            if (auto id = parseIdentity(j["identity"].dump()))
                state.identity = *id;
        }
        if (j.contains("committed")) {
            if (auto c = chainFromJson(j["committed"]))
                state.committed = *c;
            else {
                if (error) *error = {"committed", "malformed committed chain"};
                return std::nullopt;
            }
        }
        if (j.contains("committedActionIds") && j["committedActionIds"].is_array()) {
            for (const auto& id : j["committedActionIds"])
                if (id.is_string())
                    state.committedActionIds.push_back(id.get<std::string>());
        }
        state.stateRevision = j.value("stateRevision", 1u);
        return state;
    } catch (const std::exception& ex) {
        if (error) *error = {"json", ex.what()};
        return std::nullopt;
    }
}

} // namespace mastering::ipc
