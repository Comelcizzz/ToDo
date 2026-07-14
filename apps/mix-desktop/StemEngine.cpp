#include "mix-desktop/StemEngine.h"

#include <algorithm>
#include <cmath>

namespace mastering::desktop {

struct StemEngine::PlaybackTrack {
    project::TrackRecord record;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioTransportSource transport;
    dsp::ProcessorChain processor;
    juce::AudioBuffer<float> scratch;
};

StemEngine::StemEngine()
{
    formatManager_.registerBasicFormats();
}

StemEngine::~StemEngine()
{
    releaseResources();
}

void StemEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    const juce::ScopedLock guard(lock_);
    outputSampleRate_ = sampleRate;
    blockSize_ = samplesPerBlockExpected;
    masterChain_.prepare(sampleRate, 2);
    for (auto& track : tracks_) {
        track->transport.prepareToPlay(samplesPerBlockExpected, sampleRate);
        track->processor.prepare(sampleRate, 2);
        track->scratch.setSize(2, samplesPerBlockExpected, false, false, true);
    }
    prepared_ = true;
}

void StemEngine::releaseResources()
{
    const juce::ScopedLock guard(lock_);
    for (auto& track : tracks_)
        track->transport.releaseResources();
    prepared_ = false;
}

void StemEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    const juce::ScopedLock guard(lock_);
    if (tracks_.empty())
        return;

    const auto anySolo = std::ranges::any_of(tracks_, [](const auto& track) {
        return track->record.soloed;
    });

    for (auto& track : tracks_) {
        if (track->record.muted || (anySolo && !track->record.soloed))
            continue;

        track->scratch.setSize(
            2,
            bufferToFill.numSamples,
            false,
            false,
            true);
        track->scratch.clear();
        juce::AudioSourceChannelInfo trackInfo(
            &track->scratch,
            0,
            bufferToFill.numSamples);
        track->transport.getNextAudioBlock(trackInfo);
        float* trackChannels[] {
            track->scratch.getWritePointer(0),
            track->scratch.getWritePointer(1)
        };
        track->processor.process(trackChannels, 2, bufferToFill.numSamples);

        const auto gain = static_cast<float>(dsp::dbToGain(track->record.gainDb));
        const auto pan = std::clamp(track->record.pan, -1.0, 1.0);
        const auto leftGain = gain * static_cast<float>(std::sqrt((1.0 - pan) * 0.5));
        const auto rightGain = gain * static_cast<float>(std::sqrt((1.0 + pan) * 0.5));
        const auto polarity = track->record.polarityInverted ? -1.0f : 1.0f;

        bufferToFill.buffer->addFrom(
            0,
            bufferToFill.startSample,
            track->scratch,
            0,
            0,
            bufferToFill.numSamples,
            leftGain * polarity);
        if (bufferToFill.buffer->getNumChannels() > 1) {
            bufferToFill.buffer->addFrom(
                1,
                bufferToFill.startSample,
                track->scratch,
                1,
                0,
                bufferToFill.numSamples,
                rightGain * polarity);
        }
    }

    std::array<float*, 2> outputChannels {
        bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample),
        bufferToFill.buffer->getNumChannels() > 1
            ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
            : nullptr
    };
    masterChain_.process(
        outputChannels.data(),
        bufferToFill.buffer->getNumChannels() > 1 ? 2 : 1,
        bufferToFill.numSamples);
}

std::vector<project::TrackRecord> StemEngine::importFiles(
    const juce::Array<juce::File>& files)
{
    stop();
    std::vector<project::TrackRecord> imported;
    const juce::ScopedLock guard(lock_);
    for (const auto& file : files) {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(file));
        if (reader == nullptr)
            continue;

        project::TrackRecord record;
        record.id = project::makeProjectId();
        record.name = file.getFileNameWithoutExtension().toStdString();
        record.audioPath = file.getFullPathName().toStdString();
        record.role = project::inferRoleFromFilename(file.getFileName().toStdString());
        record.metrics = analyzeFile(*reader);
        imported.push_back(record);
        if (auto playback = createPlaybackTrack(record))
            tracks_.push_back(std::move(playback));
    }
    return imported;
}

void StemEngine::loadProject(const project::ProjectDocument& project)
{
    stop();
    const juce::ScopedLock guard(lock_);
    tracks_.clear();
    masterChain_.setSettings(project.masterProcessing);
    for (const auto& record : project.tracks) {
        if (auto playback = createPlaybackTrack(record))
            tracks_.push_back(std::move(playback));
    }
}

void StemEngine::updateTrack(const project::TrackRecord& record)
{
    const juce::ScopedLock guard(lock_);
    const auto iterator = std::ranges::find_if(tracks_, [&record](const auto& track) {
        return track->record.id == record.id;
    });
    if (iterator != tracks_.end()) {
        (*iterator)->record = record;
        (*iterator)->processor.setSettings(record.processing);
    }
}

void StemEngine::applyPlan(const assistant::MixPlan& plan)
{
    const juce::ScopedLock guard(lock_);
    for (const auto& adjustment : plan.trackAdjustments) {
        const auto iterator = std::ranges::find_if(tracks_, [&adjustment](const auto& track) {
            return track->record.id == adjustment.trackId;
        });
        if (iterator == tracks_.end())
            continue;
        (*iterator)->record.gainDb += adjustment.gainDeltaDb;
        (*iterator)->record.processing = adjustment.processing;
        (*iterator)->processor.setSettings(adjustment.processing);
    }
    masterChain_.setSettings(plan.masterProcessing);
}

void StemEngine::togglePlayback()
{
    const juce::ScopedLock guard(lock_);
    const auto shouldStart = !isPlaying();
    for (auto& track : tracks_) {
        if (shouldStart) {
            if (track->transport.getCurrentPosition() >= track->transport.getLengthInSeconds())
                track->transport.setPosition(0.0);
            track->transport.start();
        } else {
            track->transport.stop();
        }
    }
}

void StemEngine::stop()
{
    const juce::ScopedLock guard(lock_);
    for (auto& track : tracks_) {
        track->transport.stop();
        track->transport.setPosition(0.0);
    }
}

bool StemEngine::isPlaying() const
{
    return !tracks_.empty() && tracks_.front()->transport.isPlaying();
}

double StemEngine::positionSeconds() const
{
    return tracks_.empty() ? 0.0 : tracks_.front()->transport.getCurrentPosition();
}

double StemEngine::durationSeconds() const
{
    double duration = 0.0;
    for (const auto& track : tracks_)
        duration = std::max(duration, track->transport.getLengthInSeconds());
    return duration;
}

bool StemEngine::renderMaster(
    const project::ProjectDocument& project,
    const juce::File& destination,
    int bitsPerSample,
    juce::String& errorMessage)
{
    if (project.tracks.empty()) {
        errorMessage = "Import at least one stem before exporting.";
        return false;
    }

    struct RenderTrack {
        project::TrackRecord record;
        std::unique_ptr<juce::AudioFormatReader> reader;
        dsp::ProcessorChain processor;
    };
    std::vector<RenderTrack> renderTracks;
    juce::int64 maximumLength = 0;
    for (const auto& track : project.tracks) {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(juce::File(track.audioPath)));
        if (reader == nullptr) {
            errorMessage = "Could not open stem: " + juce::String(track.audioPath);
            return false;
        }
        if (std::abs(reader->sampleRate - project.sampleRate) > 1.0) {
            errorMessage = "All stems must use the project sample rate.";
            return false;
        }
        maximumLength = std::max(maximumLength, reader->lengthInSamples);
        dsp::ProcessorChain processor;
        processor.prepare(project.sampleRate, 2);
        processor.setSettings(track.processing);
        renderTracks.push_back({track, std::move(reader), std::move(processor)});
    }

    destination.deleteFile();
    auto output = destination.createOutputStream();
    if (output == nullptr) {
        errorMessage = "Could not create the destination WAV file.";
        return false;
    }
    juce::WavAudioFormat format;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(
        output.release(),
        project.sampleRate,
        2,
        bitsPerSample,
        {},
        0));
    if (writer == nullptr) {
        errorMessage = "Could not initialize the WAV writer.";
        return false;
    }

    constexpr int renderBlockSize = 2'048;
    juce::AudioBuffer<float> mix(2, renderBlockSize);
    juce::AudioBuffer<float> stem(2, renderBlockSize);
    dsp::ProcessorChain master;
    master.prepare(project.sampleRate, 2);
    master.setSettings(project.masterProcessing);
    const auto anySolo = std::ranges::any_of(project.tracks, [](const auto& track) {
        return track.soloed;
    });

    for (juce::int64 position = 0; position < maximumLength; position += renderBlockSize) {
        const auto samples = static_cast<int>(
            std::min<juce::int64>(renderBlockSize, maximumLength - position));
        mix.clear();
        for (auto& track : renderTracks) {
            if (track.record.muted || (anySolo && !track.record.soloed))
                continue;
            stem.clear();
            track.reader->read(&stem, 0, samples, position, true, true);
            float* channels[] {stem.getWritePointer(0), stem.getWritePointer(1)};
            track.processor.process(channels, 2, samples);

            const auto gain = static_cast<float>(dsp::dbToGain(track.record.gainDb));
            const auto pan = std::clamp(track.record.pan, -1.0, 1.0);
            const auto polarity = track.record.polarityInverted ? -1.0f : 1.0f;
            mix.addFrom(
                0,
                0,
                stem,
                0,
                0,
                samples,
                gain * static_cast<float>(std::sqrt((1.0 - pan) * 0.5)) * polarity);
            mix.addFrom(
                1,
                0,
                stem,
                1,
                0,
                samples,
                gain * static_cast<float>(std::sqrt((1.0 + pan) * 0.5)) * polarity);
        }
        float* mixChannels[] {mix.getWritePointer(0), mix.getWritePointer(1)};
        master.process(mixChannels, 2, samples);
        if (!writer->writeFromAudioSampleBuffer(mix, 0, samples)) {
            errorMessage = "Disk write failed before the master was complete.";
            return false;
        }
    }
    return true;
}

std::unique_ptr<StemEngine::PlaybackTrack> StemEngine::createPlaybackTrack(
    const project::TrackRecord& record)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(juce::File(record.audioPath)));
    if (reader == nullptr)
        return {};

    auto track = std::make_unique<PlaybackTrack>();
    track->record = record;
    const auto sourceSampleRate = reader->sampleRate;
    track->source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    track->transport.setSource(track->source.get(), 0, nullptr, sourceSampleRate);
    track->processor.setSettings(record.processing);
    if (prepared_) {
        track->transport.prepareToPlay(blockSize_, outputSampleRate_);
        track->processor.prepare(outputSampleRate_, 2);
        track->scratch.setSize(2, blockSize_);
    }
    return track;
}

analysis::AudioMetrics StemEngine::analyzeFile(juce::AudioFormatReader& reader) const
{
    constexpr double maximumAnalysisSeconds = 15.0 * 60.0;
    const auto frameCount = static_cast<int>(
        std::min<double>(reader.lengthInSamples, reader.sampleRate * maximumAnalysisSeconds));
    const auto channelCount = juce::jlimit(1, 2, static_cast<int>(reader.numChannels));
    juce::AudioBuffer<float> buffer(channelCount, frameCount);
    reader.read(&buffer, 0, frameCount, 0, true, channelCount > 1);

    std::vector<std::vector<float>> channels(
        static_cast<std::size_t>(channelCount),
        std::vector<float>(static_cast<std::size_t>(frameCount)));
    for (int channel = 0; channel < channelCount; ++channel)
        std::copy_n(buffer.getReadPointer(channel), frameCount, channels[channel].begin());

    return analysis::AudioAnalyzer {}.analyze(channels, reader.sampleRate);
}

} // namespace mastering::desktop
