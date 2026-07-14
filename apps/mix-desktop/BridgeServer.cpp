#include "mix-desktop/BridgeServer.h"

namespace mastering::desktop {
namespace {
constexpr int bridgePort = 58'432;
}

class BridgeServer::ClientConnection final : public juce::InterprocessConnection {
public:
    explicit ClientConnection(BridgeServer& owner)
        : juce::InterprocessConnection(true, 0x4d415542),
          owner_(owner)
    {
    }

private:
    void connectionMade() override {}
    void connectionLost() override {}
    void messageReceived(const juce::MemoryBlock& message) override
    {
        owner_.handleMessage(message);
    }

    BridgeServer& owner_;
};

BridgeServer::BridgeServer() = default;

BridgeServer::~BridgeServer()
{
    stop();
}

bool BridgeServer::start()
{
    return beginWaitingForSocket(bridgePort, "127.0.0.1");
}

void BridgeServer::stop()
{
    stopWaitingForSocket();
}

void BridgeServer::setAnalysisHandler(
    std::function<void(const juce::var&)> handler)
{
    analysisHandler_ = std::move(handler);
}

juce::InterprocessConnection* BridgeServer::createConnectionObject()
{
    return new ClientConnection(*this);
}

void BridgeServer::handleMessage(const juce::MemoryBlock& message)
{
    const auto text = juce::String::fromUTF8(
        static_cast<const char*>(message.getData()),
        static_cast<int>(message.getSize()));
    const auto parsed = juce::JSON::parse(text);
    if (!parsed.isVoid() && analysisHandler_)
        analysisHandler_(parsed);
}

} // namespace mastering::desktop
