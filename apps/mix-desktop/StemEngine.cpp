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
    truePeakLimiter_.prepare(sampleRate);
    truePeakLimiter_.setCeilingDb(-1.0);
    kickBassSeparator_.prepare(sampleRate);
    kickBassSeparator_.setDepthDb(3.0);
    referenceTransport_.prepareToPlay(samplesPerBlockExpected, sampleRate);
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
    referenceTransport_.releaseResources();
    prepared_ = false;
}

void StemEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    const juce::ScopedLock guard(lock_);

    if (monitorSource_ == MonitorSource::reference && referenceSource_ != nullptr) {
        juce::AudioBuffer<float> referenceScratch(2, bufferToFill.numSamples);
        referenceScratch.clear();
        juce::AudioSourceChannelInfo info(&referenceScratch, 0, bufferToFill.numSamples);
        referenceTransport_.getNextAudioBlock(info);
        const auto gain = static_cast<float>(dsp::dbToGain(referenceGainDb_));
        bufferToFill.buffer->addFrom(
            0,
            bufferToFill.startSample,
            referenceScratch,
            0,
            0,
            bufferToFill.numSamples,
            gain);
        if (bufferToFill.buffer->getNumChannels() > 1) {
            bufferToFill.buffer->addFrom(
                1,
                bufferToFill.startSample,
                referenceScratch,
                1,
                0,
                bufferToFill.numSamples,
                gain);
        }
        return;
    }

    if (tracks_.empty())
        return;

    const auto anySolo = std::ranges::any_of(tracks_, [](const auto& track) {
        return track->record.soloed;
    });

    PlaybackTrack* kickTrack = nullptr;
    PlaybackTrack* bassTrack = nullptr;

    for (auto& track : tracks_) {
        if (track->record.muted || (anySolo && !track->record.soloed))
            continue;

        track->scratch.setSize(2, bufferToFill.numSamples, false, false, true);
        track->scratch.clear();
        juce::AudioSourceChannelInfo trackInfo(&track->scratch, 0, bufferToFill.numSamples);
        track->transport.getNextAudioBlock(trackInfo);
        float* trackChannels[] {
            track->scratch.getWritePointer(0),
            track->scratch.getWritePointer(1)
        };
        track->processor.process(trackChannels, 2, bufferToFill.numSamples);

        if (track->record.role == project::TrackRole::kick)
            kickTrack = track.get();
        if (track->record.role == project::TrackRole::bass)
            bassTrack = track.get();
    }

    if (kickTrack != nullptr && bassTrack != nullptr) {
        const float* sidechain[] {
            kickTrack->scratch.getReadPointer(0),
            kickTrack->scratch.getReadPointer(1)
        };
        float* target[] {
            bassTrack->scratch.getWritePointer(0),
            bassTrack->scratch.getWritePointer(1)
        };
        kickBassSeparator_.process(sidechain, target, 2, bufferToFill.numSamples);
    }

    for (auto& track : tracks_) {
        if (track->record.muted || (anySolo && !track->record.soloed))
            continue;

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
    const auto channelCount = bufferToFill.buffer->getNumChannels() > 1 ? 2 : 1;
    masterChain_.process(outputChannels.data(), channelCount, bufferToFill.numSamples);
    truePeakLimiter_.process(outputChannels.data(), channelCount, bufferToFill.numSamples);
}

std::vector<project::TrackRecord> StemEngine::importFiles(
    const juce::Array<juce::File>& files)
{
    stop();
    if (files.isEmpty())
        return {};

    std::vector<project::TrackRecord> imported;
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
    }

    // Empty/failed import is a no-op: keep existing engine tracks.
    if (imported.empty())
        return {};

    const juce::ScopedLock guard(lock_);
    tracks_.clear();
    for (const auto& record : imported) {
        if (auto playback = createPlaybackTrack(record))
            tracks_.push_back(std::move(playback));
    }
    mixIntegratedLufs_ = 0.0;
    for (const auto& track : imported)
        mixIntegratedLufs_ += track.metrics.integratedLufs;
    mixIntegratedLufs_ /= static_cast<double>(imported.size());
    recalculateReferenceGain();
    return imported;
}

void StemEngine::loadProject(const project::ProjectDocument& project)
{
    stop();
    const juce::ScopedLock guard(lock_);
    tracks_.clear();
    masterChain_.setSettings(project.masterProcessing);
    mixIntegratedLufs_ = 0.0;
    for (const auto& record : project.tracks) {
        mixIntegratedLufs_ += record.metrics.integratedLufs;
        if (auto playback = createPlaybackTrack(record))
            tracks_.push_back(std::move(playback));
    }
    if (!project.tracks.empty())
        mixIntegratedLufs_ /= static_cast<double>(project.tracks.size());
    recalculateReferenceGain();
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
        if (adjustment.state == assistant::ActionState::rejected)
            continue;
        const auto iterator = std::ranges::find_if(tracks_, [&adjustment](const auto& track) {
            return track->record.id == adjustment.trackId;
        });
        if (iterator == tracks_.end())
            continue;
        (*iterator)->record.gainDb = adjustment.targetGainDb;
        (*iterator)->record.processing = adjustment.processing;
        (*iterator)->processor.setSettings(adjustment.processing);
    }
    masterChain_.setSettings(plan.masterProcessing);
}

bool StemEngine::loadReference(const juce::File& file, juce::String& errorMessage)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(file));
    if (reader == nullptr) {
        errorMessage = "Could not open the reference audio file.";
        return false;
    }

    const juce::ScopedLock guard(lock_);
    referenceMetrics_ = analyzeFile(*reader);
    const auto sourceSampleRate = reader->sampleRate;
    referenceSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    referenceTransport_.setSource(
        referenceSource_.get(),
        0,
        nullptr,
        sourceSampleRate);
    if (prepared_)
        referenceTransport_.prepareToPlay(blockSize_, outputSampleRate_);
    recalculateReferenceGain();
    return true;
}

void StemEngine::clearReference()
{
    const juce::ScopedLock guard(lock_);
    referenceTransport_.stop();
    referenceTransport_.setSource(nullptr);
    referenceSource_.reset();
    referenceMetrics_.reset();
    referenceGainDb_ = 0.0;
}

void StemEngine::setMonitorSource(MonitorSource source)
{
    const juce::ScopedLock guard(lock_);
    monitorSource_ = source;
}

StemEngine::MonitorSource StemEngine::monitorSource() const
{
    return monitorSource_;
}

bool StemEngine::hasReference() const
{
    return referenceSource_ != nullptr;
}

double StemEngine::referenceGainDb() const
{
    return referenceGainDb_;
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
    if (referenceSource_ != nullptr) {
        if (shouldStart) {
            if (referenceTransport_.getCurrentPosition()
                >= referenceTransport_.getLengthInSeconds()) {
                referenceTransport_.setPosition(0.0);
            }
            referenceTransport_.start();
        } else {
            referenceTransport_.stop();
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
    referenceTransport_.stop();
    referenceTransport_.setPosition(0.0);
}

bool StemEngine::isPlaying() const
{
    if (monitorSource_ == MonitorSource::reference && referenceSource_ != nullptr)
        return referenceTransport_.isPlaying();
    return !tracks_.empty() && tracks_.front()->transport.isPlaying();
}

double StemEngine::positionSeconds() const
{
    if (monitorSource_ == MonitorSource::reference && referenceSource_ != nullptr)
        return referenceTransport_.getCurrentPosition();
    return tracks_.empty() ? 0.0 : tracks_.front()->transport.getCurrentPosition();
}

double StemEngine::durationSeconds() const
{
    double duration = 0.0;
    for (const auto& track : tracks_)
        duration = std::max(duration, track->transport.getLengthInSeconds());
    if (referenceSource_ != nullptr)
        duration = std::max(duration, referenceTransport_.getLengthInSeconds());
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
    if (bitsPerSample != 24 && bitsPerSample != 32) {
        errorMessage = "Supported export depths are 24-bit and 32-bit float.";
        return false;
    }

    struct RenderTrack {
        project::TrackRecord record;
        std::unique_ptr<juce::AudioFormatReader> reader;
        dsp::ProcessorChain processor;
        juce::AudioBuffer<float> scratch;
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
        renderTracks.push_back({track, std::move(reader), std::move(processor), {}});
    }

    destination.deleteFile();
    auto outputStream = destination.createOutputStream();
    if (outputStream == nullptr) {
        errorMessage = "Could not create the destination WAV file.";
        return false;
    }
    juce::WavAudioFormat format;
    auto options = juce::AudioFormatWriterOptions {}
                       .withSampleRate(project.sampleRate)
                       .withNumChannels(2)
                       .withBitsPerSample(bitsPerSample)
                       .withSampleFormat(
                           bitsPerSample == 32
                               ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                               : juce::AudioFormatWriterOptions::SampleFormat::integral);
    std::unique_ptr<juce::OutputStream> ownedStream(outputStream.release());
    auto writer = format.createWriterFor(ownedStream, options);
    if (writer == nullptr) {
        errorMessage = "Could not initialize the WAV writer.";
        return false;
    }

    constexpr int renderBlockSize = 2'048;
    juce::AudioBuffer<float> mix(2, renderBlockSize);
    dsp::ProcessorChain master;
    master.prepare(project.sampleRate, 2);
    master.setSettings(project.masterProcessing);
    dsp::TruePeakLimiter limiter;
    limiter.prepare(project.sampleRate);
    limiter.setCeilingDb(-1.0);
    dsp::DynamicSeparator separator;
    separator.prepare(project.sampleRate);
    separator.setDepthDb(3.0);
    const auto anySolo = std::ranges::any_of(project.tracks, [](const auto& track) {
        return track.soloed;
    });

    for (juce::int64 position = 0; position < maximumLength; position += renderBlockSize) {
        const auto samples = static_cast<int>(
            std::min<juce::int64>(renderBlockSize, maximumLength - position));
        mix.clear();

        RenderTrack* kick = nullptr;
        RenderTrack* bass = nullptr;
        for (auto& track : renderTracks) {
            if (track.record.muted || (anySolo && !track.record.soloed))
                continue;
            track.scratch.setSize(2, samples, false, false, true);
            track.scratch.clear();
            track.reader->read(&track.scratch, 0, samples, position, true, true);
            float* channels[] {
                track.scratch.getWritePointer(0),
                track.scratch.getWritePointer(1)
            };
            track.processor.process(channels, 2, samples);
            if (track.record.role == project::TrackRole::kick)
                kick = &track;
            if (track.record.role == project::TrackRole::bass)
                bass = &track;
        }

        if (kick != nullptr && bass != nullptr) {
            const float* sidechain[] {
                kick->scratch.getReadPointer(0),
                kick->scratch.getReadPointer(1)
            };
            float* target[] {
                bass->scratch.getWritePointer(0),
                bass->scratch.getWritePointer(1)
            };
            separator.process(sidechain, target, 2, samples);
        }

        for (auto& track : renderTracks) {
            if (track.record.muted || (anySolo && !track.record.soloed))
                continue;
            const auto gain = static_cast<float>(dsp::dbToGain(track.record.gainDb));
            const auto pan = std::clamp(track.record.pan, -1.0, 1.0);
            const auto polarity = track.record.polarityInverted ? -1.0f : 1.0f;
            mix.addFrom(
                0,
                0,
                track.scratch,
                0,
                0,
                samples,
                gain * static_cast<float>(std::sqrt((1.0 - pan) * 0.5)) * polarity);
            mix.addFrom(
                1,
                0,
                track.scratch,
                1,
                0,
                samples,
                gain * static_cast<float>(std::sqrt((1.0 + pan) * 0.5)) * polarity);
        }

        float* mixChannels[] {mix.getWritePointer(0), mix.getWritePointer(1)};
        master.process(mixChannels, 2, samples);
        limiter.process(mixChannels, 2, samples);
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
        std::copy_n(buffer.getReadPointer(channel), frameCount, channels[static_cast<std::size_t>(channel)].begin());

    return analysis::AudioAnalyzer {}.analyze(channels, reader.sampleRate);
}

void StemEngine::recalculateReferenceGain()
{
    if (!referenceMetrics_) {
        referenceGainDb_ = 0.0;
        return;
    }
    referenceGainDb_ = std::clamp(
        mixIntegratedLufs_ - referenceMetrics_->integratedLufs,
        -24.0,
        24.0);
}

} // namespace mastering::desktop
