#pragma once

#include "mastering/ipc/MixNodeController.h"
#include "shared/BridgeClient.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mastering::plugin {

class MixNodeProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::Timer {
public:
    MixNodeProcessor();
    ~MixNodeProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    [[nodiscard]] mixnode::MixNodeController& controller() noexcept { return controller_; }
    [[nodiscard]] const mixnode::MixNodeController& controller() const noexcept { return controller_; }
    [[nodiscard]] bool bridgeConnected() const noexcept;
    [[nodiscard]] juce::AudioProcessorValueTreeState& apvts() noexcept { return state_; }

    void setRolePreset(ipc::MixNodeRolePreset role);
    void setChannelPosition(ipc::ChannelPosition pos);
    void setTrackName(const juce::String& name);
    void notifyLatencyChanged();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void handleBridgeMessage(const juce::String& json);
    void registerWithSuite();
    void syncIdentityFromState();
    void pushLocalAutomationToController();
    [[nodiscard]] bool hasSidechain() const noexcept;

    juce::AudioProcessorValueTreeState state_;
    mixnode::MixNodeController controller_;
    app::BridgeClient bridge_;
    juce::AudioBuffer<float> floatScratch_;
    juce::AudioBuffer<float> sidechainScratch_;
    int lastReportedLatency_ {-1};
    bool prepared_ {false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixNodeProcessor)
};

} // namespace mastering::plugin
