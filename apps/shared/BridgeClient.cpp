#include "shared/BridgeClient.h"

namespace mastering::app {
namespace {
constexpr int bridgePort = 58'432;
}

BridgeClient::BridgeClient()
    : juce::InterprocessConnection(true, 0x4d415542)
{
}

BridgeClient::~BridgeClient()
{
    stop();
}

void BridgeClient::start()
{
    timerCallback();
    startTimer(2'000);
}

void BridgeClient::stop()
{
    stopTimer();
    disconnect();
    connected_.store(false, std::memory_order_relaxed);
}

bool BridgeClient::connected() const noexcept
{
    return connected_.load(std::memory_order_relaxed);
}

bool BridgeClient::sendJson(const juce::String& json)
{
    if (!connected())
        return false;
    return sendMessage(juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8()));
}

void BridgeClient::setMessageHandler(
    std::function<void(const juce::String&)> handler)
{
    messageHandler_ = std::move(handler);
}

void BridgeClient::connectionMade()
{
    connected_.store(true, std::memory_order_relaxed);
}

void BridgeClient::connectionLost()
{
    connected_.store(false, std::memory_order_relaxed);
}

void BridgeClient::messageReceived(const juce::MemoryBlock& message)
{
    if (messageHandler_) {
        messageHandler_(juce::String::fromUTF8(
            static_cast<const char*>(message.getData()),
            static_cast<int>(message.getSize())));
    }
}

void BridgeClient::timerCallback()
{
    if (!connected())
        connectToSocket("127.0.0.1", bridgePort, 250);
}

} // namespace mastering::app
