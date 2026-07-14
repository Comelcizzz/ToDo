#pragma once

#include "mastering/assistant/MixAdvisor.h"
#include "mastering/dsp/DynamicTools.h"
#include "mastering/project/ProjectDocument.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <optional>
#include <vector>

namespace mastering::desktop {

class StemEngine final : public juce::AudioSource {
public:
    enum class MonitorSource {
        mix,
        reference
    };

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
    [[nodiscard]] bool loadReference(const juce::File& file, juce::String& errorMessage);
    void clearReference();
    void setMonitorSource(MonitorSource source);
    [[nodiscard]] MonitorSource monitorSource() const;
    [[nodiscard]] bool hasReference() const;
    [[nodiscard]] double referenceGainDb() const;

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
    void recalculateReferenceGain();

    juce::AudioFormatManager formatManager_;
    std::vector<std::unique_ptr<PlaybackTrack>> tracks_;
    std::unique_ptr<juce::AudioFormatReaderSource> referenceSource_;
    juce::AudioTransportSource referenceTransport_;
    dsp::ProcessorChain masterChain_;
    dsp::TruePeakLimiter truePeakLimiter_;
    dsp::DynamicSeparator kickBassSeparator_;
    std::optional<analysis::AudioMetrics> referenceMetrics_;
    MonitorSource monitorSource_ {MonitorSource::mix};
    double referenceGainDb_ {0.0};
    double mixIntegratedLufs_ {-120.0};
    double outputSampleRate_ {48'000.0};
    int blockSize_ {512};
    bool prepared_ {false};
    mutable juce::CriticalSection lock_;
};

} // namespace mastering::desktop
