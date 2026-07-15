#pragma once

#include "mastering/assistant/MixAdvisor.h"
#include "mastering/assistant/SectionAutomation.h"
#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/MasterSafetyChain.h"
#include "mastering/dsp/ParallelCompressor.h"
#include "mastering/dsp/StereoWidth.h"
#include "mastering/dsp/VocalRider.h"
#include "mastering/project/ProjectDocument.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
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

    enum class CompareMode {
        raw,
        autoProcessed,
        current,
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
    void applyMixPassAction(const project::MixPassAction& action);
    void setSectionAutomation(const assistant::SectionAutomationState& state);
    [[nodiscard]] bool loadReference(const juce::File& file, juce::String& errorMessage);
    void clearReference();
    void setMonitorSource(MonitorSource source);
    [[nodiscard]] MonitorSource monitorSource() const;
    void setCompareMode(CompareMode mode);
    [[nodiscard]] CompareMode compareMode() const;
    [[nodiscard]] bool hasReference() const;
    [[nodiscard]] double referenceGainDb() const;
    [[nodiscard]] double compareMatchGainDb() const;

    // Store last known integrated LUFS for a compare mode (from render or preview).
    void setModeIntegratedLufs(CompareMode mode, double integratedLufs);
    void recalculateCompareMatchGain();

    void togglePlayback();
    void stop();
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double positionSeconds() const;
    [[nodiscard]] double durationSeconds() const;

    [[nodiscard]] bool renderMaster(
        const project::ProjectDocument& project,
        const juce::File& destination,
        int bitsPerSample,
        juce::String& errorMessage,
        CompareMode mode = CompareMode::current);

private:
    struct PlaybackTrack;
    [[nodiscard]] std::unique_ptr<PlaybackTrack> createPlaybackTrack(
        const project::TrackRecord& track);
    [[nodiscard]] analysis::AudioMetrics analyzeFile(
        juce::AudioFormatReader& reader) const;
    void recalculateReferenceGain();
    void syncParallelStereoFromRecord(PlaybackTrack& track);
    void applySectionParameterAutomation(PlaybackTrack& track, double timeSeconds);
    static int modeIndex(CompareMode mode) noexcept;

    juce::AudioFormatManager formatManager_;
    std::vector<std::unique_ptr<PlaybackTrack>> tracks_;
    std::unique_ptr<juce::AudioFormatReaderSource> referenceSource_;
    juce::AudioTransportSource referenceTransport_;
    dsp::ProcessorChain masterChain_;
    dsp::MasterSafetyChain masterSafety_;
    dsp::DynamicSeparator kickBassSeparator_;
    assistant::SectionAutomationState sectionAutomation_;
    project::ProjectDocument sectionProject_;
    std::optional<analysis::AudioMetrics> referenceMetrics_;
    MonitorSource monitorSource_ {MonitorSource::mix};
    CompareMode compareMode_ {CompareMode::current};
    double referenceGainDb_ {0.0};
    double mixIntegratedLufs_ {-120.0};
    double compareMatchGainDb_ {0.0};
    std::array<double, 4> modeLufs_ {-120.0, -120.0, -120.0, -120.0};
    std::array<bool, 4> modeLufsValid_ {false, false, false, false};
    double outputSampleRate_ {48'000.0};
    int blockSize_ {512};
    bool prepared_ {false};
    mutable juce::CriticalSection lock_;
};

} // namespace mastering::desktop
