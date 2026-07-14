#pragma once

#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace mastering::desktop {

class BridgeServer final : private juce::InterprocessConnectionServer {
public:
    BridgeServer();
    ~BridgeServer() override;

    bool start();
    void stop();
    void setAnalysisHandler(std::function<void(const juce::var&)> handler);

private:
    class ClientConnection;
    juce::InterprocessConnection* createConnectionObject() override;
    void handleMessage(const juce::MemoryBlock& message);

    std::function<void(const juce::var&)> analysisHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeServer)
};

} // namespace mastering::desktop
