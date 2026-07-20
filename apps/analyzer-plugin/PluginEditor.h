#pragma once

#include "analyzer-plugin/PluginProcessor.h"
#include "shared/WebViewComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mastering::plugin {

class AnalyzerEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer {
public:
    explicit AnalyzerEditor(AnalyzerProcessor& processor);
    ~AnalyzerEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void handleCommand(const juce::var& command);
    void pushState();

    AnalyzerProcessor& processor_;
    app::WebViewComponent webView_ {app::WebViewComponent::Product::analyzerPlugin};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerEditor)
};

} // namespace mastering::plugin
