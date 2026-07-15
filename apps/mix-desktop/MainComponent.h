#pragma once

#include "mix-desktop/BridgeServer.h"
#include "mix-desktop/StemEngine.h"
#include "shared/WebViewComponent.h"
#include "mastering/ipc/MixNodeProtocol.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <optional>
#include <vector>

namespace mastering::desktop {

class MainComponent final : public juce::AudioAppComponent,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void handleCommand(const juce::var& command);
    void handleBridgeAnalysis(const juce::var& report);
    void handleMixNodeMessage(const ipc::MixNodeEnvelope& env, const juce::String& raw);
    void sendMixNodeAction(const juce::String& messageType, const juce::var& command);
    void createProject();
    void chooseProjectToOpen();
    void chooseStems();
    void chooseReference();
    void chooseMasterDestination(int bitsPerSample);
    void chooseResearchDestination();
    void importStems(const juce::Array<juce::File>& files);
    void saveProject(bool chooseDestination);
    void openProject(const juce::File& file);
    void generateMixPlan();
    void applyMixPlan();
    void selectVariant(const juce::String& variant);
    void pushState();
    [[nodiscard]] project::TrackRecord* findTrack(const juce::String& id);

    app::WebViewComponent webView_ {app::WebViewComponent::Product::desktopSuite};
    StemEngine engine_;
    BridgeServer bridge_;
    assistant::MixAdvisor advisor_;
    project::ProjectDocument project_;
    std::vector<assistant::MixPlan> planVariants_;
    assistant::MixPlan currentPlan_;
    assistant::MixVariant selectedVariant_ {assistant::MixVariant::balanced};
    std::optional<analysis::AudioMetrics> referenceMetrics_;
    int exportBitDepth_ {24};
    juce::File projectFile_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::String selectedMixNodeId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace mastering::desktop
