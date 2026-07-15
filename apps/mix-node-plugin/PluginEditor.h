#pragma once

#include "mix-node-plugin/PluginProcessor.h"
#include "shared/WebViewComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mastering::plugin {

class MixNodeEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer {
public:
    explicit MixNodeEditor(MixNodeProcessor& processor);
    ~MixNodeEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void handleCommand(const juce::var& command);
    void pushState();

    MixNodeProcessor& processor_;
    app::WebViewComponent webView_ {app::WebViewComponent::Product::mixNode};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixNodeEditor)
};

} // namespace mastering::plugin
