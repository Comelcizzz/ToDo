#include "mix-node-plugin/PluginProcessor.h"
#include "mix-node-plugin/PluginEditor.h"
#include "mastering/ipc/MixNodeProtocol.h"
#include "mastering/project/ProjectDocument.h"

#include <cmath>

namespace mastering::plugin {
namespace {

juce::StringArray rolePresetNames()
{
    using ipc::MixNodeRolePreset;
    return {
        "Custom",
        "Rhythm Guitar Left",
        "Rhythm Guitar Right",
        "Secondary Rhythm Left",
        "Secondary Rhythm Right",
        "Lead Guitar",
        "Clean Guitar",
        "Guitar Bus",
        "Bass",
        "Bass Bus",
        "Kick",
        "Snare",
        "Drum Bus",
        "Lead Vocal",
        "Scream Vocal",
        "Vocal Bus",
        "Synth",
        "Music Bus",
        "Master"
    };
}

ipc::MixNodeRolePreset roleFromChoice(int index)
{
    const auto names = rolePresetNames();
    if (juce::isPositiveAndBelow(index, names.size()))
        if (auto r = ipc::rolePresetFromString(names[index].toStdString()))
            return *r;
    return ipc::MixNodeRolePreset::custom;
}

} // namespace

MixNodeProcessor::MixNodeProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state_(*this, nullptr, "MixNodeState", createParameters())
{
    state_.state.setProperty("instanceId", juce::String(project::makeProjectId()), nullptr);
    state_.state.setProperty("projectId", "", nullptr);
    state_.state.setProperty("sessionId", "", nullptr);
    state_.state.setProperty("trackId", "", nullptr);
    state_.state.setProperty("trackName", "Mix Node", nullptr);
    state_.state.setProperty("pairId", "", nullptr);
    state_.state.setProperty("parentBusId", "", nullptr);
    state_.addParameterListener("inputGainDb", this);
    state_.addParameterListener("outputGainDb", this);
    state_.addParameterListener("eqFreq", this);
    state_.addParameterListener("eqGain", this);
    state_.addParameterListener("dynThreshold", this);
    state_.addParameterListener("dynMaxCut", this);
    state_.addParameterListener("satDrive", this);
    state_.addParameterListener("bypass", this);
    syncIdentityFromState();
    bridge_.setMessageHandler([this](const juce::String& json) { handleBridgeMessage(json); });
    bridge_.start();
    startTimerHz(2);
}

MixNodeProcessor::~MixNodeProcessor()
{
    stopTimer();
    bridge_.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout MixNodeProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"inputGainDb", 1}, "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"outputGainDb", 1}, "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"eqFreq", 1}, "EQ Frequency",
        juce::NormalisableRange<float>(40.0f, 12'000.0f, 0.1f, 0.3f), 1'000.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"eqGain", 1}, "EQ Gain",
        juce::NormalisableRange<float>(-18.0f, 18.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"dynThreshold", 1}, "DynEQ Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"dynMaxCut", 1}, "DynEQ Max Cut",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 8.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {"satDrive", 1}, "Saturation Drive",
        juce::NormalisableRange<float>(1.0f, 8.0f, 0.01f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID {"bypass", 1}, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"rolePreset", 1}, "Role", rolePresetNames(), 0));
    return layout;
}

void MixNodeProcessor::syncIdentityFromState()
{
    ipc::MixNodeIdentity id;
    id.instanceId = state_.state.getProperty("instanceId").toString().toStdString();
    id.projectId = state_.state.getProperty("projectId").toString().toStdString();
    id.sessionId = state_.state.getProperty("sessionId").toString().toStdString();
    id.trackId = state_.state.getProperty("trackId").toString().toStdString();
    id.trackName = state_.state.getProperty("trackName").toString().toStdString();
    id.pairId = state_.state.getProperty("pairId").toString().toStdString();
    id.parentBusId = state_.state.getProperty("parentBusId").toString().toStdString();
    if (auto* choice = state_.getParameter("rolePreset"))
        id.role = roleFromChoice(static_cast<int>(choice->convertFrom0to1(choice->getValue()) + 0.5f));
    id.channelPosition = ipc::ChannelPosition::stereo;
    controller_.setIdentity(id);
}

void MixNodeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const auto channels = std::max(1, getTotalNumOutputChannels());
    controller_.prepareAudio(sampleRate, samplesPerBlock, channels);
    floatScratch_.setSize(2, std::max(1, samplesPerBlock), false, false, true);
    sidechainScratch_.setSize(2, std::max(1, samplesPerBlock), false, false, true);
    prepared_ = true;
    pushLocalAutomationToController();
    notifyLatencyChanged();
    registerWithSuite();
}

void MixNodeProcessor::releaseResources()
{
    prepared_ = false;
    controller_.resetAudio();
}

bool MixNodeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (!(in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()))
        return false;
    if (out != in)
        return false;
    const auto sc = layouts.getChannelSet(true, 1);
    if (!sc.isDisabled()
        && sc != juce::AudioChannelSet::mono()
        && sc != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

bool MixNodeProcessor::hasSidechain() const noexcept
{
    return getBusCount(true) > 1 && !getBus(true, 1)->getCurrentLayout().isDisabled();
}

void MixNodeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto mainBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    const auto channels = std::min({2, mainBus.getNumChannels(), outBus.getNumChannels()});
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    // Ensure output starts from input (in-place for typical layouts).
    if (mainBus.getArrayOfReadPointers() != outBus.getArrayOfWritePointers()) {
        for (int c = 0; c < channels; ++c)
            outBus.copyFrom(c, 0, mainBus, c, 0, samples);
    }

    float* mainPtrs[2] {nullptr, nullptr};
    for (int c = 0; c < channels; ++c)
        mainPtrs[c] = outBus.getWritePointer(c);

    const float* scPtrs[2] {nullptr, nullptr};
    bool useSc = false;
    if (hasSidechain() && getBusCount(true) > 1) {
        auto scBus = getBusBuffer(buffer, true, 1);
        if (scBus.getNumChannels() > 0 && scBus.getNumSamples() >= samples) {
            useSc = true;
            scPtrs[0] = scBus.getReadPointer(0);
            if (channels > 1) {
                scPtrs[1] = scBus.getNumChannels() > 1 ? scBus.getReadPointer(1) : scPtrs[0];
            }
        }
    }

    controller_.processAudio(mainPtrs, useSc ? scPtrs : nullptr, channels, samples);
}

void MixNodeProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    floatScratch_.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            floatScratch_.setSample(c, i, static_cast<float>(buffer.getSample(c, i)));
    processBlock(floatScratch_, midi);
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(c, i, floatScratch_.getSample(c, i));
}

void MixNodeProcessor::notifyLatencyChanged()
{
    const auto latency = controller_.latencySamples();
    if (latency != lastReportedLatency_) {
        lastReportedLatency_ = latency;
        setLatencySamples(latency);
    }
}

void MixNodeProcessor::parameterChanged(const juce::String&, float)
{
    if (!prepared_)
        return;
    pushLocalAutomationToController();
    notifyLatencyChanged();
}

void MixNodeProcessor::pushLocalAutomationToController()
{
    auto snap = controller_.snapshot();
    auto chain = snap.committed;
    if (auto* p = state_.getRawParameterValue("inputGainDb"))
        chain.inputGainDb = p->load();
    if (auto* p = state_.getRawParameterValue("outputGainDb"))
        chain.outputGainDb = p->load();
    if (auto* p = state_.getRawParameterValue("eqFreq"))
        chain.staticEq.frequencyHz = p->load();
    if (auto* p = state_.getRawParameterValue("eqGain"))
        chain.staticEq.gainDb = p->load();
    if (auto* p = state_.getRawParameterValue("dynThreshold"))
        chain.dynamicEq.bands[0].thresholdDb = p->load();
    if (auto* p = state_.getRawParameterValue("dynMaxCut"))
        chain.dynamicEq.bands[0].maxCutDb = p->load();
    if (auto* p = state_.getRawParameterValue("satDrive")) {
        chain.saturation.drive = p->load();
        chain.saturationEnabled = chain.saturation.drive > 1.05;
    }
    if (auto* p = state_.getRawParameterValue("bypass"))
        chain.bypass = p->load() >= 0.5f;
    controller_.applyAutomationState(chain);
}

void MixNodeProcessor::setRolePreset(ipc::MixNodeRolePreset role)
{
    const auto name = juce::String(ipc::rolePresetToString(role));
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(state_.getParameter("rolePreset"))) {
        const int idx = rolePresetNames().indexOf(name);
        if (idx >= 0)
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(idx)));
    }
    syncIdentityFromState();
}

void MixNodeProcessor::setChannelPosition(ipc::ChannelPosition pos)
{
    state_.state.setProperty("channelPosition", juce::String(ipc::channelPositionToString(pos)), nullptr);
    syncIdentityFromState();
}

void MixNodeProcessor::setTrackName(const juce::String& name)
{
    state_.state.setProperty("trackName", name, nullptr);
    syncIdentityFromState();
}

bool MixNodeProcessor::bridgeConnected() const noexcept
{
    return bridge_.connected();
}

void MixNodeProcessor::registerWithSuite()
{
    syncIdentityFromState();
    auto payload = ipc::serializeHostState(controller_.hostState());
    auto env = controller_.makeEnvelope(ipc::MixNodeMessageType::RegisterInstance, payload, true);
    bridge_.sendJson(juce::String(ipc::serializeEnvelope(env)));
}

void MixNodeProcessor::handleBridgeMessage(const juce::String& json)
{
    ipc::MixNodeProtocolError err;
    auto env = ipc::parseEnvelope(json.toStdString(), &err);
    if (!env)
        return;
    std::string response;
    const auto result = controller_.handleEnvelope(*env, &response);
    if (env->requiresAck || !response.empty()) {
        auto ack = controller_.makeEnvelope(
            result == mixnode::ApplyResult::ok || result == mixnode::ApplyResult::idempotentReplay
                ? ipc::MixNodeMessageType::StateUpdated
                : ipc::MixNodeMessageType::Error,
            response.empty() ? "{}" : response,
            false);
        bridge_.sendJson(juce::String(ipc::serializeEnvelope(ack)));
    }
    notifyLatencyChanged();
}

void MixNodeProcessor::timerCallback()
{
    controller_.tick(juce::Time::currentTimeMillis());
    if (bridge_.connected()) {
        auto env = controller_.makeEnvelope(ipc::MixNodeMessageType::Heartbeat, "{}", false);
        bridge_.sendJson(juce::String(ipc::serializeEnvelope(env)));
    } else {
        controller_.onSuiteDisconnected();
    }
}

void MixNodeProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    // Persist committed host state only — never temporary preview.
    const auto json = ipc::serializeHostState(controller_.hostState());
    destination.replaceAll(json.data(), static_cast<size_t>(json.size()));
}

void MixNodeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const std::string json(static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes));
    ipc::MixNodeProtocolError err;
    if (!controller_.restoreHostState(json, &err))
        return; // malformed / newer schema — keep defaults, do not crash
    const auto id = controller_.identity();
    state_.state.setProperty("instanceId", juce::String(id.instanceId), nullptr);
    state_.state.setProperty("projectId", juce::String(id.projectId), nullptr);
    state_.state.setProperty("sessionId", juce::String(id.sessionId), nullptr);
    state_.state.setProperty("trackId", juce::String(id.trackId), nullptr);
    state_.state.setProperty("trackName", juce::String(id.trackName), nullptr);
    notifyLatencyChanged();
}

const juce::String MixNodeProcessor::getName() const
{
    return "Mastering Audio Mix Node";
}

juce::AudioProcessorEditor* MixNodeProcessor::createEditor()
{
    return new MixNodeEditor(*this);
}

} // namespace mastering::plugin

// JUCE entry points
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mastering::plugin::MixNodeProcessor();
}
