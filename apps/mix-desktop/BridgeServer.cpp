#include "mix-desktop/BridgeServer.h"
#include "mastering/ipc/BridgeProtocol.h"

#include <algorithm>

namespace mastering::desktop {
namespace {
constexpr int bridgePort = mastering::ipc::kBridgePort;
}

class BridgeServer::ClientConnection final : public juce::InterprocessConnection {
public:
    explicit ClientConnection(BridgeServer& owner)
        : juce::InterprocessConnection(true, mastering::ipc::kBridgeMagic),
          owner_(owner)
    {
    }

    void sendJson(const juce::String& json)
    {
        sendMessage(juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8()));
    }

private:
    void connectionMade() override
    {
        std::lock_guard lock(owner_.mutex_);
        owner_.clients_.push_back(this);
    }

    void connectionLost() override
    {
        std::lock_guard lock(owner_.mutex_);
        owner_.clients_.erase(
            std::remove(owner_.clients_.begin(), owner_.clients_.end(), this),
            owner_.clients_.end());
    }

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
    juce::InterprocessConnectionServer::stop();
}

void BridgeServer::setAnalysisHandler(std::function<void(const juce::var&)> handler)
{
    analysisHandler_ = std::move(handler);
}

void BridgeServer::setMixNodeHandler(
    std::function<void(const ipc::MixNodeEnvelope&, const juce::String&)> handler)
{
    mixNodeHandler_ = std::move(handler);
}

void BridgeServer::setSuiteSession(const juce::String& projectId, const juce::String& sessionId)
{
    std::lock_guard lock(mutex_);
    projectId_ = projectId;
    sessionId_ = sessionId;
}

juce::String BridgeServer::suiteProjectId() const
{
    std::lock_guard lock(mutex_);
    return projectId_;
}

juce::String BridgeServer::suiteSessionId() const
{
    std::lock_guard lock(mutex_);
    return sessionId_;
}

bool BridgeServer::sendToAll(const juce::String& json)
{
    std::lock_guard lock(mutex_);
    const juce::MemoryBlock block(json.toRawUTF8(), json.getNumBytesAsUTF8());
    bool any = false;
    for (auto* client : clients_) {
        if (client != nullptr) {
            client->sendMessage(block);
            any = true;
        }
    }
    return any;
}

std::vector<MixNodeConnectionInfo> BridgeServer::mixNodes() const
{
    std::lock_guard lock(mutex_);
    std::vector<MixNodeConnectionInfo> out;
    out.reserve(mixNodes_.size());
    for (const auto& [_, info] : mixNodes_)
        out.push_back(info);
    return out;
}

void BridgeServer::upsertMixNode(const MixNodeConnectionInfo& info)
{
    std::lock_guard lock(mutex_);
    mixNodes_[info.identity.instanceId] = info;
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
    const auto stdText = text.toStdString();

    // Mix Node protocol messages carry mixNodeProtocolVersion.
    if (stdText.find("\"mixNodeProtocolVersion\"") != std::string::npos) {
        ipc::MixNodeProtocolError err;
        if (auto env = ipc::parseEnvelope(stdText, &err)) {
            if (mixNodeHandler_)
                mixNodeHandler_(*env, text);
            return;
        }
        return;
    }

    const auto validation = mastering::ipc::validateTrackAnalysisPayload(stdText);
    if (!validation.ok)
        return;
    const auto parsed = juce::JSON::parse(text);
    if (!parsed.isVoid() && analysisHandler_)
        analysisHandler_(parsed);
}

} // namespace mastering::desktop
