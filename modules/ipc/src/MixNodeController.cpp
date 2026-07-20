#include "mastering/ipc/MixNodeController.h"

#include <algorithm>
#include <chrono>

namespace mastering::mixnode {
namespace {

std::int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string makeMessageId()
{
    static std::atomic<std::uint64_t> counter {1};
    return "msg-" + std::to_string(counter.fetch_add(1));
}

dsp::MixNodeChainState applyScalarAction(
    dsp::MixNodeChainState base,
    const ipc::MixNodeAction& action)
{
    if (action.proposedChain)
        return *action.proposedChain;

    if (action.processorId == "inputGain" && action.parameterId == "gainDb")
        base.inputGainDb = action.proposedValue;
    else if (action.processorId == "outputGain" && action.parameterId == "gainDb")
        base.outputGainDb = action.proposedValue;
    else if (action.processorId == "bypass" && action.parameterId == "enabled")
        base.bypass = action.proposedValue >= 0.5;
    else if (action.processorId == "staticEq") {
        if (action.parameterId == "frequencyHz")
            base.staticEq.frequencyHz = action.proposedValue;
        else if (action.parameterId == "gainDb")
            base.staticEq.gainDb = action.proposedValue;
        else if (action.parameterId == "q")
            base.staticEq.q = action.proposedValue;
        else if (action.parameterId == "bypass")
            base.staticEq.bypass = action.proposedValue >= 0.5;
    } else if (action.processorId == "dynamicEq") {
        auto& band = base.dynamicEq.bands[0];
        if (action.parameterId == "thresholdDb")
            band.thresholdDb = action.proposedValue;
        else if (action.parameterId == "maxCutDb")
            band.maxCutDb = action.proposedValue;
        else if (action.parameterId == "frequencyHz")
            band.frequencyHz = action.proposedValue;
        else if (action.parameterId == "q")
            band.q = action.proposedValue;
        else if (action.parameterId == "ratio")
            band.ratio = action.proposedValue;
        else if (action.parameterId == "attackMs")
            band.attackMs = action.proposedValue;
        else if (action.parameterId == "releaseMs")
            band.releaseMs = action.proposedValue;
    } else if (action.processorId == "saturation") {
        if (action.parameterId == "drive")
            base.saturation.drive = action.proposedValue;
        else if (action.parameterId == "mix")
            base.saturation.mix = action.proposedValue;
        else if (action.parameterId == "enabled")
            base.saturationEnabled = action.proposedValue >= 0.5;
        else if (action.parameterId == "oversamplingFactor")
            base.saturation.oversamplingFactor = static_cast<int>(action.proposedValue);
    }
    ++base.revision;
    return base;
}

} // namespace

MixNodeController::MixNodeController()
{
    committed_.dynamicEq.bandCount = 1;
    committed_.dynamicEq.bands[0].bandId = 1;
    preview_ = committed_;
    published_.store(std::make_shared<const dsp::MixNodeChainState>(committed_), std::memory_order_release);
}

void MixNodeController::setIdentity(const ipc::MixNodeIdentity& identity)
{
    std::lock_guard lock(mutex_);
    identity_ = identity;
}

ipc::MixNodeIdentity MixNodeController::identity() const
{
    std::lock_guard lock(mutex_);
    return identity_;
}

void MixNodeController::prepareAudio(double sampleRate, int maxBlock, int channels)
{
    std::lock_guard lock(mutex_);
    audioChain_.prepare(sampleRate, maxBlock, channels);
    audioChain_.setState(previewActive_ ? preview_ : committed_);
    published_.store(
        std::make_shared<const dsp::MixNodeChainState>(previewActive_ ? preview_ : committed_),
        std::memory_order_release);
    publishedRevision_.store(stateRevision_, std::memory_order_release);
}

void MixNodeController::resetAudio()
{
    std::lock_guard lock(mutex_);
    audioChain_.reset();
}

void MixNodeController::publishActiveToAudio() noexcept
{
    // Allocates shared_ptr on message thread only.
    published_.store(
        std::make_shared<const dsp::MixNodeChainState>(previewActive_ ? preview_ : committed_),
        std::memory_order_release);
    publishedRevision_.store(stateRevision_, std::memory_order_release);
}

void MixNodeController::processAudio(
    float* const* main,
    const float* const* sidechain,
    int channelCount,
    int sampleCount) noexcept
{
    // Audio thread: load immutable snapshot (atomic shared_ptr), apply continuous params, process.
    // No mutex, socket, JSON, or file I/O.
    if (const auto snap = published_.load(std::memory_order_acquire))
        audioChain_.setContinuousParameters(*snap);
    audioChain_.process(main, sidechain, channelCount, sampleCount);
}

int MixNodeController::latencySamples() const noexcept
{
    return audioChain_.latencySamples();
}

dsp::MixNodeChainMeters MixNodeController::meters() const noexcept
{
    return audioChain_.meters();
}

bool MixNodeController::actionTargetsThisInstance(
    const ipc::MixNodeAction& action,
    ipc::MixNodeProtocolError* error) const
{
    if (!identity_.projectId.empty() && action.projectId != identity_.projectId) {
        if (error) *error = {"projectId", "wrong project"};
        return false;
    }
    if (!identity_.sessionId.empty() && action.sessionId != identity_.sessionId) {
        if (error) *error = {"sessionId", "wrong session"};
        return false;
    }
    if (!identity_.instanceId.empty() && action.targetInstanceId != identity_.instanceId) {
        if (error) *error = {"instanceId", "wrong instance"};
        return false;
    }
    return true;
}

bool MixNodeController::alreadyApplied(const ipc::MixNodeAction& action) const
{
    const auto key = action.actionId + ":" + std::to_string(action.actionVersion);
    return std::find(committedActionIds_.begin(), committedActionIds_.end(), key)
        != committedActionIds_.end();
}

void MixNodeController::pushUndo(const dsp::MixNodeChainState& previous, const std::string& actionId)
{
    undoStack_.push_back({actionId, previous});
    if (undoStack_.size() > kUndoDepth)
        undoStack_.erase(undoStack_.begin());
}

ApplyResult MixNodeController::preview(const ipc::MixNodeAction& action)
{
    std::lock_guard lock(mutex_);
    ipc::MixNodeProtocolError err;
    if (!actionTargetsThisInstance(action, &err)) {
        ++errorCount_;
        return ApplyResult::rejectedWrongIds;
    }
    if (!action.proposedChain
        && (action.proposedValue < action.allowedMin || action.proposedValue > action.allowedMax)) {
        ++errorCount_;
        return ApplyResult::rejectedRange;
    }
    preview_ = applyScalarAction(committed_, action);
    previewActive_ = true;
    previewActionId_ = action.actionId;
    previewStartedMs_ = nowMs();
    status_ = "previewing";
    localModified_ = false;
    publishActiveToAudio();
    return ApplyResult::ok;
}

ApplyResult MixNodeController::commit(const ipc::MixNodeAction& action)
{
    std::lock_guard lock(mutex_);
    ipc::MixNodeProtocolError err;
    if (!actionTargetsThisInstance(action, &err)) {
        ++errorCount_;
        return ApplyResult::rejectedWrongIds;
    }
    if (alreadyApplied(action))
        return ApplyResult::idempotentReplay;

    if (localModified_) {
        status_ = "conflict";
        ++errorCount_;
        return ApplyResult::rejectedStale;
    }

    const auto previous = committed_;
    committed_ = applyScalarAction(committed_, action);
    pushUndo(previous, action.actionId);
    committedActionIds_.push_back(action.actionId + ":" + std::to_string(action.actionVersion));
    if (committedActionIds_.size() > 256)
        committedActionIds_.erase(committedActionIds_.begin());
    previewActive_ = false;
    previewActionId_.clear();
    ++stateRevision_;
    status_ = "committed";
    publishActiveToAudio();
    return ApplyResult::ok;
}

ApplyResult MixNodeController::cancelPreview()
{
    std::lock_guard lock(mutex_);
    previewActive_ = false;
    previewActionId_.clear();
    preview_ = committed_;
    status_ = suiteConnected_ ? "committed" : "disconnected";
    publishActiveToAudio();
    return ApplyResult::ok;
}

ApplyResult MixNodeController::undo()
{
    std::lock_guard lock(mutex_);
    if (undoStack_.empty())
        return ApplyResult::error;
    const auto previous = undoStack_.back().second;
    undoStack_.pop_back();
    committed_ = previous;
    previewActive_ = false;
    preview_ = committed_;
    ++stateRevision_;
    status_ = "committed";
    publishActiveToAudio();
    return ApplyResult::ok;
}

ApplyResult MixNodeController::reject(const ipc::MixNodeAction& action)
{
    std::lock_guard lock(mutex_);
    ipc::MixNodeProtocolError err;
    if (!actionTargetsThisInstance(action, &err))
        return ApplyResult::rejectedWrongIds;
    return ApplyResult::ok;
}

void MixNodeController::applyLocalEdit(const dsp::MixNodeChainState& state)
{
    std::lock_guard lock(mutex_);
    pushUndo(committed_, "local-edit");
    committed_ = state;
    ++committed_.revision;
    previewActive_ = false;
    preview_ = committed_;
    ++stateRevision_;
    localModified_ = true;
    status_ = "local-modified";
    publishActiveToAudio();
}

void MixNodeController::applyAutomationState(const dsp::MixNodeChainState& state)
{
    std::lock_guard lock(mutex_);
    if (previewActive_)
        return; // do not clobber active preview with automation
    committed_ = state;
    preview_ = committed_;
    ++stateRevision_;
    publishActiveToAudio();
}

void MixNodeController::onSuiteDisconnected()
{
    std::lock_guard lock(mutex_);
    suiteConnected_ = false;
    if (previewActive_) {
        previewActive_ = false;
        preview_ = committed_;
        publishActiveToAudio();
    }
    status_ = "disconnected";
}

void MixNodeController::onSuiteConnected()
{
    std::lock_guard lock(mutex_);
    suiteConnected_ = true;
    if (!previewActive_ && !localModified_)
        status_ = "committed";
}

void MixNodeController::tick(std::int64_t now)
{
    std::lock_guard lock(mutex_);
    if (previewActive_ && previewStartedMs_ > 0
        && (now - previewStartedMs_) > kPreviewTimeoutMs) {
        previewActive_ = false;
        preview_ = committed_;
        previewActionId_.clear();
        status_ = "committed";
        publishActiveToAudio();
    }
}

MixNodeRuntimeSnapshot MixNodeController::snapshot() const
{
    std::lock_guard lock(mutex_);
    MixNodeRuntimeSnapshot s;
    s.identity = identity_;
    s.committed = committed_;
    s.active = previewActive_ ? preview_ : committed_;
    s.previewActive = previewActive_;
    s.previewActionId = previewActionId_;
    s.stateRevision = stateRevision_;
    s.queueOverflowCount = queueOverflowCount_;
    s.errorCount = errorCount_;
    s.suiteConnected = suiteConnected_;
    s.degraded = queueOverflowCount_ > 0 || errorCount_ > 0;
    s.status = status_;
    return s;
}

ipc::MixNodeHostState MixNodeController::hostState() const
{
    std::lock_guard lock(mutex_);
    ipc::MixNodeHostState hs;
    hs.identity = identity_;
    hs.committed = committed_;
    hs.committedActionIds = committedActionIds_;
    hs.stateRevision = stateRevision_;
    hs.schemaVersion = dsp::kMixNodeChainSchemaVersion;
    return hs;
}

bool MixNodeController::restoreHostState(std::string_view json, ipc::MixNodeProtocolError* error)
{
    auto parsed = ipc::parseHostState(json, error);
    if (!parsed)
        return false;
    std::lock_guard lock(mutex_);
    identity_ = parsed->identity;
    committed_ = parsed->committed;
    committedActionIds_ = parsed->committedActionIds;
    stateRevision_ = parsed->stateRevision;
    previewActive_ = false;
    preview_ = committed_;
    status_ = "committed";
    localModified_ = false;
    audioChain_.setState(committed_);
    published_.store(std::make_shared<const dsp::MixNodeChainState>(committed_), std::memory_order_release);
    publishedRevision_.store(stateRevision_, std::memory_order_release);
    return true;
}

ipc::MixNodeEnvelope MixNodeController::makeEnvelope(
    ipc::MixNodeMessageType type,
    std::string payloadJson,
    bool requiresAck) const
{
    std::lock_guard lock(mutex_);
    ipc::MixNodeEnvelope env;
    env.mixNodeProtocolVersion = ipc::kMixNodeProtocolVersion;
    env.projectId = identity_.projectId;
    env.sessionId = identity_.sessionId;
    env.instanceId = identity_.instanceId;
    env.messageId = makeMessageId();
    env.timestampMs = nowMs();
    env.type = type;
    env.payloadJson = std::move(payloadJson);
    env.requiresAck = requiresAck;
    return env;
}

ApplyResult MixNodeController::handleEnvelope(
    const ipc::MixNodeEnvelope& env,
    std::string* responsePayload)
{
    if (env.type != ipc::MixNodeMessageType::Hello
        && env.type != ipc::MixNodeMessageType::RegisterInstance
        && env.type != ipc::MixNodeMessageType::RequestState) {
        const auto id = identity();
        if (!id.instanceId.empty() && env.instanceId != id.instanceId) {
            ++errorCount_;
            return ApplyResult::rejectedWrongIds;
        }
        if (!id.projectId.empty() && !env.projectId.empty() && env.projectId != id.projectId) {
            ++errorCount_;
            return ApplyResult::rejectedWrongIds;
        }
        if (!id.sessionId.empty() && !env.sessionId.empty() && env.sessionId != id.sessionId) {
            ++errorCount_;
            return ApplyResult::rejectedWrongIds;
        }
    }

    switch (env.type) {
    case ipc::MixNodeMessageType::Hello:
    case ipc::MixNodeMessageType::Heartbeat:
        onSuiteConnected();
        if (responsePayload)
            *responsePayload = ipc::serializeHostState(hostState());
        return ApplyResult::ok;
    case ipc::MixNodeMessageType::RequestState:
        if (responsePayload)
            *responsePayload = ipc::serializeHostState(hostState());
        return ApplyResult::ok;
    case ipc::MixNodeMessageType::PreviewAction: {
        auto action = ipc::parseAction(env.payloadJson);
        if (!action)
            return ApplyResult::rejectedMalformed;
        return preview(*action);
    }
    case ipc::MixNodeMessageType::CommitAction: {
        auto action = ipc::parseAction(env.payloadJson);
        if (!action)
            return ApplyResult::rejectedMalformed;
        return commit(*action);
    }
    case ipc::MixNodeMessageType::CancelPreview:
        return cancelPreview();
    case ipc::MixNodeMessageType::Undo:
        return undo();
    case ipc::MixNodeMessageType::Disconnect:
        onSuiteDisconnected();
        return ApplyResult::ok;
    default:
        return ApplyResult::error;
    }
}

} // namespace mastering::mixnode
