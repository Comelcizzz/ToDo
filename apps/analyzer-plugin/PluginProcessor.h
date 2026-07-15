#pragma once

#include "shared/BridgeClient.h"
#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/project/ProjectDocument.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mastering::plugin {

class AnalyzerProcessor final : public juce::AudioProcessor {
public:
    AnalyzerProcessor();
    ~AnalyzerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& name) override;
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    [[nodiscard]] analysis::AudioMetrics metrics() const noexcept;
    [[nodiscard]] project::TrackRole role() const noexcept;
    void setRole(project::TrackRole role);
    [[nodiscard]] bool bridgeConnected() const noexcept;
    [[nodiscard]] juce::String projectId() const;
    void publishAnalysis(bool writeSidecarWhenOffline);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();

    juce::AudioProcessorValueTreeState state_;
    analysis::RealtimeMeter meter_;
    app::BridgeClient bridge_;
    juce::AudioBuffer<float> floatMeterScratch_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerProcessor)
};

} // namespace mastering::plugin
