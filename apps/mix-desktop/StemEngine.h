#pragma once

#include "mastering/assistant/MixAdvisor.h"
#include "mastering/project/ProjectDocument.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <vector>

namespace mastering::desktop {

class StemEngine final : public juce::AudioSource {
public:
    StemEngine();
    ~StemEngine() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    [[nodiscard]] std::vector<project::TrackRecord> importFiles(
        const juce::Array<juce::File>& files);
    void loadProject(const project::ProjectDocument& project);
    void updateTrack(const project::TrackRecord& track);
    void applyPlan(const assistant::MixPlan& plan);

    void togglePlayback();
    void stop();
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double positionSeconds() const;
    [[nodiscard]] double durationSeconds() const;

    [[nodiscard]] bool renderMaster(
        const project::ProjectDocument& project,
        const juce::File& destination,
        int bitsPerSample,
        juce::String& errorMessage);

private:
    struct PlaybackTrack;
    [[nodiscard]] std::unique_ptr<PlaybackTrack> createPlaybackTrack(
        const project::TrackRecord& track);
    [[nodiscard]] analysis::AudioMetrics analyzeFile(
        juce::AudioFormatReader& reader) const;

    juce::AudioFormatManager formatManager_;
    std::vector<std::unique_ptr<PlaybackTrack>> tracks_;
    dsp::ProcessorChain masterChain_;
    double outputSampleRate_ {48'000.0};
    int blockSize_ {512};
    bool prepared_ {false};
    mutable juce::CriticalSection lock_;
};

} // namespace mastering::desktop
