#include "analyzer-plugin/PluginEditor.h"

namespace mastering::plugin {

AnalyzerEditor::AnalyzerEditor(AnalyzerProcessor& processor)
    : AudioProcessorEditor(processor),
      processor_(processor)
{
    addAndMakeVisible(webView_);
    webView_.setCommandHandler([this](const juce::var& command) {
        handleCommand(command);
    });
    setResizable(true, true);
    setResizeLimits(520, 560, 900, 900);
    setSize(620, 680);
    startTimerHz(10);
}

AnalyzerEditor::~AnalyzerEditor()
{
    stopTimer();
}

void AnalyzerEditor::resized()
{
    webView_.setBounds(getLocalBounds());
}

void AnalyzerEditor::timerCallback()
{
    pushState();
}

void AnalyzerEditor::handleCommand(const juce::var& command)
{
    const auto* object = command.getDynamicObject();
    if (object == nullptr)
        return;

    const auto type = object->getProperty("type").toString();
    if (type == "ui-ready") {
        pushState();
    } else if (type == "analyze") {
        processor_.publishAnalysis(true);
        pushState();
    } else if (type == "set-role") {
        if (const auto role = project::roleFromString(
            object->getProperty("role").toString().toStdString())) {
            processor_.setRole(*role);
            pushState();
        }
    }
}

void AnalyzerEditor::pushState()
{
    auto state = juce::DynamicObject::Ptr(new juce::DynamicObject());
    state->setProperty("product", "plugin");
    state->setProperty("connected", processor_.bridgeConnected());
    state->setProperty("projectId", processor_.projectId());
    state->setProperty("projectName", "FL Studio session");
    state->setProperty("playing", false);
    state->setProperty("positionSeconds", 0.0);
    state->setProperty("durationSeconds", processor_.metrics().durationSeconds);
    state->setProperty("tracks", juce::Array<juce::var> {});
    state->setProperty("suggestions", juce::Array<juce::var> {});
    state->setProperty("analyzerRole", project::roleToString(processor_.role()));
    state->setProperty(
        "analyzerMetrics",
        juce::JSON::parse(juce::String(analysis::toJson(processor_.metrics()))));
    webView_.pushState(juce::var(state.get()));
}

} // namespace mastering::plugin
