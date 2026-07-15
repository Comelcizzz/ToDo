#pragma once

#include "mastering/ipc/MixNodeProtocol.h"

#include <juce_events/juce_events.h>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mastering::desktop {

struct MixNodeConnectionInfo {
    ipc::MixNodeIdentity identity {};
    dsp::MixNodeChainState lastKnown {};
    std::uint32_t stateRevision {1};
    bool connected {false};
    std::int64_t lastHeartbeatMs {0};
    int latencySamples {0};
    double sampleRate {48'000.0};
    bool sidechainActive {false};
    std::string status {"disconnected"};
};

class BridgeServer final : private juce::InterprocessConnectionServer {
public:
    BridgeServer();
    ~BridgeServer() override;

    bool start();
    void stop();
    void setAnalysisHandler(std::function<void(const juce::var&)> handler);
    void setMixNodeHandler(std::function<void(const ipc::MixNodeEnvelope&, const juce::String& raw)> handler);

    bool sendToAll(const juce::String& json);
    [[nodiscard]] std::vector<MixNodeConnectionInfo> mixNodes() const;
    void upsertMixNode(const MixNodeConnectionInfo& info);
    void setSuiteSession(const juce::String& projectId, const juce::String& sessionId);
    [[nodiscard]] juce::String suiteProjectId() const;
    [[nodiscard]] juce::String suiteSessionId() const;

private:
    class ClientConnection;
    juce::InterprocessConnection* createConnectionObject() override;
    void handleMessage(const juce::MemoryBlock& message);
    void broadcast(const juce::MemoryBlock& message);

    std::function<void(const juce::var&)> analysisHandler_;
    std::function<void(const ipc::MixNodeEnvelope&, const juce::String&)> mixNodeHandler_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MixNodeConnectionInfo> mixNodes_;
    juce::String projectId_;
    juce::String sessionId_;
    std::vector<ClientConnection*> clients_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeServer)
};

} // namespace mastering::desktop
