#pragma once

#include "mastering/ipc/MixNodeProtocol.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mastering::mixnode {

inline constexpr int kPreviewTimeoutMs = 30'000;
inline constexpr std::size_t kCommandQueueCapacity = 64;
inline constexpr std::size_t kUndoDepth = 8;

enum class ApplyResult {
    ok,
    idempotentReplay,
    rejectedWrongIds,
    rejectedRange,
    rejectedStale,
    rejectedMalformed,
    queueOverflow,
    error
};

struct MixNodeRuntimeSnapshot {
    ipc::MixNodeIdentity identity {};
    dsp::MixNodeChainState active {};
    dsp::MixNodeChainState committed {};
    bool previewActive {false};
    std::string previewActionId;
    std::uint32_t stateRevision {1};
    std::uint64_t queueOverflowCount {0};
    std::uint64_t errorCount {0};
    bool suiteConnected {false};
    bool degraded {false};
    std::string status {"committed"};
};

class MixNodeController {
public:
    MixNodeController();

    void setIdentity(const ipc::MixNodeIdentity& identity);
    [[nodiscard]] ipc::MixNodeIdentity identity() const;

    void prepareAudio(double sampleRate, int maxBlock, int channels);
    void resetAudio();
    void processAudio(
        float* const* main,
        const float* const* sidechain,
        int channelCount,
        int sampleCount) noexcept;

    [[nodiscard]] int latencySamples() const noexcept;
    [[nodiscard]] dsp::MixNodeChainMeters meters() const noexcept;

    [[nodiscard]] ApplyResult preview(const ipc::MixNodeAction& action);
    [[nodiscard]] ApplyResult commit(const ipc::MixNodeAction& action);
    [[nodiscard]] ApplyResult cancelPreview();
    [[nodiscard]] ApplyResult undo();
    [[nodiscard]] ApplyResult reject(const ipc::MixNodeAction& action);
    void applyLocalEdit(const dsp::MixNodeChainState& state);
    // Host automation / APVTS sync — does not mark Suite conflict.
    void applyAutomationState(const dsp::MixNodeChainState& state);
    void onSuiteDisconnected();
    void onSuiteConnected();
    void tick(std::int64_t nowMs);

    [[nodiscard]] MixNodeRuntimeSnapshot snapshot() const;
    [[nodiscard]] ipc::MixNodeHostState hostState() const;
    [[nodiscard]] bool restoreHostState(std::string_view json, ipc::MixNodeProtocolError* error = nullptr);

    [[nodiscard]] ipc::MixNodeEnvelope makeEnvelope(
        ipc::MixNodeMessageType type,
        std::string payloadJson,
        bool requiresAck = false) const;

    [[nodiscard]] ApplyResult handleEnvelope(const ipc::MixNodeEnvelope& env, std::string* responsePayload = nullptr);

private:
    void publishActiveToAudio() noexcept;
    [[nodiscard]] bool actionTargetsThisInstance(const ipc::MixNodeAction& action, ipc::MixNodeProtocolError* error) const;
    [[nodiscard]] bool alreadyApplied(const ipc::MixNodeAction& action) const;
    void pushUndo(const dsp::MixNodeChainState& previous, const std::string& actionId);

    mutable std::mutex mutex_;
    ipc::MixNodeIdentity identity_ {};
    dsp::MixNodeChainState committed_ {};
    dsp::MixNodeChainState preview_ {};
    bool previewActive_ {false};
    std::string previewActionId_;
    std::int64_t previewStartedMs_ {0};
    std::uint32_t stateRevision_ {1};
    std::vector<std::string> committedActionIds_;
    std::vector<std::pair<std::string, dsp::MixNodeChainState>> undoStack_;
    std::uint64_t queueOverflowCount_ {0};
    std::uint64_t errorCount_ {0};
    bool suiteConnected_ {false};
    bool localModified_ {false};
    std::string status_ {"committed"};

    dsp::MixNodeChain audioChain_;
    std::atomic<std::shared_ptr<const dsp::MixNodeChainState>> published_ {
        std::make_shared<const dsp::MixNodeChainState>()};
    std::atomic<std::uint32_t> publishedRevision_ {0};
};

} // namespace mastering::mixnode
