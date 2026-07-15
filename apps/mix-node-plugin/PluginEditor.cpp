#include "mix-node-plugin/PluginEditor.h"
#include "mastering/dsp/ParameterSmoother.h"
#include "mastering/ipc/MixNodeProtocol.h"
#include "mastering/product/ProductVersion.h"

namespace mastering::plugin {

MixNodeEditor::MixNodeEditor(MixNodeProcessor& ownedProcessor)
    : AudioProcessorEditor(ownedProcessor),
      processor_(ownedProcessor)
{
    addAndMakeVisible(webView_);
    webView_.setCommandHandler([this](const juce::var& command) { handleCommand(command); });
    setResizable(true, true);
    setResizeLimits(420, 520, 900, 1000);
    setSize(520, 640);
    startTimerHz(12);
}

MixNodeEditor::~MixNodeEditor()
{
    stopTimer();
}

void MixNodeEditor::resized()
{
    webView_.setBounds(getLocalBounds());
}

void MixNodeEditor::timerCallback()
{
    pushState();
}

void MixNodeEditor::handleCommand(const juce::var& command)
{
    const auto* object = command.getDynamicObject();
    if (object == nullptr)
        return;
    const auto type = object->getProperty("type").toString();
    if (type == "ui-ready") {
        pushState();
    } else if (type == "set-role") {
        if (auto role = ipc::rolePresetFromString(object->getProperty("role").toString().toStdString()))
            processor_.setRolePreset(*role);
    } else if (type == "set-track-name") {
        processor_.setTrackName(object->getProperty("name").toString());
    } else if (type == "mix-node-cancel-preview") {
        (void) processor_.controller().cancelPreview();
    } else if (type == "mix-node-undo") {
        (void) processor_.controller().undo();
    } else if (type == "mix-node-set-param") {
        const auto param = object->getProperty("parameterId").toString();
        const float value = static_cast<float>(object->getProperty("value"));
        if (auto* p = processor_.apvts().getParameter(param))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    }
    pushState();
}

void MixNodeEditor::pushState()
{
    const auto snap = processor_.controller().snapshot();
    const auto meters = processor_.controller().meters();
    auto state = juce::DynamicObject::Ptr(new juce::DynamicObject());
    state->setProperty("product", "mix-node");
    state->setProperty("productVersion", juce::String(product::currentProductVersion().display()));
    state->setProperty("connected", processor_.bridgeConnected());
    state->setProperty("projectId", juce::String(snap.identity.projectId));
    state->setProperty("sessionId", juce::String(snap.identity.sessionId));
    state->setProperty("instanceId", juce::String(snap.identity.instanceId));
    state->setProperty("projectName", juce::String(snap.identity.trackName));
    state->setProperty("trackName", juce::String(snap.identity.trackName));
    state->setProperty("role", juce::String(ipc::rolePresetToString(snap.identity.role)));
    state->setProperty("channelPosition", juce::String(ipc::channelPositionToString(snap.identity.channelPosition)));
    state->setProperty("pairId", juce::String(snap.identity.pairId));
    state->setProperty("status", juce::String(snap.status));
    state->setProperty("previewActive", snap.previewActive);
    state->setProperty("stateRevision", static_cast<int>(snap.stateRevision));
    state->setProperty("latencySamples", processor_.controller().latencySamples());
    state->setProperty("sampleRate", processor_.getSampleRate());
    state->setProperty("bypass", snap.active.bypass);
    state->setProperty("inputGainDb", snap.active.inputGainDb);
    state->setProperty("outputGainDb", snap.active.outputGainDb);
    state->setProperty("eqFreq", snap.active.staticEq.frequencyHz);
    state->setProperty("eqGain", snap.active.staticEq.gainDb);
    state->setProperty("dynThreshold", snap.active.dynamicEq.bands[0].thresholdDb);
    state->setProperty("dynMaxCut", snap.active.dynamicEq.bands[0].maxCutDb);
    state->setProperty("satDrive", snap.active.saturation.drive);
    state->setProperty("saturationEnabled", snap.active.saturationEnabled);
    state->setProperty("inputPeakDb", mastering::dsp::gainToDbSafe(std::max(meters.inputPeakL, meters.inputPeakR)));
    state->setProperty("outputPeakDb", mastering::dsp::gainToDbSafe(std::max(meters.outputPeakL, meters.outputPeakR)));
    state->setProperty("grDb", meters.dynamicEq.bands[0].currentGrDb);
    state->setProperty("sidechainPeakDb", mastering::dsp::gainToDbSafe(meters.sidechainPeak));
    state->setProperty("degraded", snap.degraded);
    state->setProperty("playing", false);
    state->setProperty("positionSeconds", 0.0);
    state->setProperty("durationSeconds", 0.0);
    state->setProperty("tracks", juce::Array<juce::var> {});
    state->setProperty("suggestions", juce::Array<juce::var> {});
    webView_.pushState(juce::var(state.get()));
}

} // namespace mastering::plugin
