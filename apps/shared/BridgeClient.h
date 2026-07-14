#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace mastering::app {

class BridgeClient final : private juce::InterprocessConnection,
                           private juce::Timer {
public:
    BridgeClient();
    ~BridgeClient() override;

    void start();
    void stop();
    [[nodiscard]] bool connected() const noexcept;
    bool sendJson(const juce::String& json);
    void setMessageHandler(std::function<void(const juce::String&)> handler);

private:
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived(const juce::MemoryBlock& message) override;
    void timerCallback() override;

    std::atomic<bool> connected_ {false};
    std::function<void(const juce::String&)> messageHandler_;
};

} // namespace mastering::app
