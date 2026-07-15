#include "mastering/analysis/StreamingAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mastering::analysis {
namespace {

constexpr int kBassBinCount = 30; // 30–180 Hz → ~5 Hz bins
constexpr double kBassLowHz = 30.0;
constexpr double kBassHighHz = 180.0;
constexpr double kKickMinSpacing = 0.2;
constexpr double kSnareMinSpacing = 0.12;
constexpr int kChunkFrames = 4096;

int scaleFrames(int refFrames, double sampleRate) noexcept
{
    const double scale = sampleRate / 48'000.0;
    const int scaled = static_cast<int>(std::lround(double(refFrames) * scale));
    return std::max(64, scaled);
}

double rmsDb(const float* x, int n) noexcept
{
    if (x == nullptr || n <= 0)
        return -120.0;
    double a = 0.0;
    for (int i = 0; i < n; ++i)
        a += double(x[i]) * double(x[i]);
    return 20.0 * std::log10(std::max(1.0e-12, std::sqrt(a / double(n))));
}

double peakDb(const float* x, int n) noexcept
{
    if (x == nullptr || n <= 0)
        return -120.0;
    double p = 0.0;
    for (int i = 0; i < n; ++i)
        p = std::max(p, std::abs(double(x[i])));
    return 20.0 * std::log10(std::max(1.0e-12, p));
}

void pushActivity(std::vector<std::uint8_t>& lane, int frameIndex, bool on)
{
    if (frameIndex < 0)
        return;
    if (static_cast<int>(lane.size()) <= frameIndex)
        lane.resize(static_cast<std::size_t>(frameIndex) + 1, 0);
    if (on)
        lane[static_cast<std::size_t>(frameIndex)] = 1;
}

std::vector<ActivityMask> collapseActivity(
    const std::vector<std::uint8_t>& lane,
    const std::string& kind,
    double sampleRate,
    double confidence)
{
    std::vector<ActivityMask> out;
    if (lane.empty() || sampleRate <= 0.0)
        return out;
    bool in = false;
    double start = 0.0;
    for (std::size_t i = 0; i <= lane.size(); ++i) {
        const bool on = i < lane.size() && lane[i] != 0;
        if (on && !in) {
            in = true;
            start = double(i) / sampleRate;
        } else if (!on && in) {
            ActivityMask m;
            m.kind = kind;
            m.startSeconds = start;
            m.endSeconds = double(i) / sampleRate;
            m.confidence = confidence;
            out.push_back(m);
            in = false;
        }
    }
    return out;
}

} // namespace

AnalysisWindowPresets AnalysisWindowPresets::forSampleRate(double sampleRate) noexcept
{
    AnalysisWindowPresets p;
    p.lowFreq.sizeFrames = scaleFrames(4096, sampleRate);
    p.lowFreq.hopFrames = scaleFrames(1024, sampleRate);
    p.transient.sizeFrames = scaleFrames(512, sampleRate);
    p.transient.hopFrames = scaleFrames(128, sampleRate);
    p.presence.sizeFrames = scaleFrames(2048, sampleRate);
    p.presence.hopFrames = scaleFrames(512, sampleRate);
    p.fizz.sizeFrames = scaleFrames(1024, sampleRate);
    p.fizz.hopFrames = scaleFrames(256, sampleRate);
    return p;
}

void StreamingAnalyzer::reset() noexcept
{
    progress_ = 0.0;
    framesProcessed_ = 0;
    kickEvents_.clear();
    snareEvents_.clear();
    peakCandidates_.clear();
    bassBinHz_.clear();
    bassEnergySum_.clear();
    bassEnergyCount_.clear();
    fundamentalSamples_.clear();
    overlapBuffer_.clear();
    sumSquares_ = 0.0;
    peakAbs_ = 0.0;
    lastKickTime_ = -1.0e9;
    lastSnareTime_ = -1.0e9;
    presenceEnergySum_ = 0.0;
    fizzEnergySum_ = 0.0;
    lowMidEnergySum_ = 0.0;
    activeFrames_ = 0;
    totalAnalysisFrames_ = 0;
    activityKick_.clear();
    activitySnare_.clear();
    activityPresence_.clear();
    activityFizz_.clear();
}

void StreamingAnalyzer::setTrackIdentity(
    std::string trackId,
    std::uint64_t fileSize,
    std::uint64_t mtimeHash)
{
    trackId_ = std::move(trackId);
    fileSize_ = fileSize;
    mtimeHash_ = mtimeHash;
}

void StreamingAnalyzer::setSectionMarkers(std::vector<AnalysisSectionMarker> sections)
{
    sections_ = std::move(sections);
}

bool StreamingAnalyzer::isCancelled() const noexcept
{
    return cancelFlag_ != nullptr && cancelFlag_->load(std::memory_order_relaxed);
}

std::string StreamingAnalyzer::cacheKey() const
{
    // trackId + fileSize + mtime hash string
    std::ostringstream oss;
    oss << trackId_ << '|' << fileSize_ << '|' << std::hex << mtimeHash_;
    return oss.str();
}

void StreamingAnalyzer::begin(double sampleRate)
{
    reset();
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    windows_ = AnalysisWindowPresets::forSampleRate(sampleRate_);

    bassBinHz_.resize(static_cast<std::size_t>(kBassBinCount));
    bassEnergySum_.assign(static_cast<std::size_t>(kBassBinCount), 0.0);
    bassEnergyCount_.assign(static_cast<std::size_t>(kBassBinCount), 0);
    const double binWidth = (kBassHighHz - kBassLowHz) / double(kBassBinCount);
    for (int i = 0; i < kBassBinCount; ++i)
        bassBinHz_[static_cast<std::size_t>(i)] = kBassLowHz + (double(i) + 0.5) * binWidth;

    overlapBuffer_.reserve(static_cast<std::size_t>(windows_.lowFreq.sizeFrames * 2));
}

void StreamingAnalyzer::updateProgress(double secondsProcessed, double estimatedTotal)
{
    if (estimatedTotal > 0.0)
        progress_ = std::clamp(secondsProcessed / estimatedTotal, 0.0, 1.0);
    else
        progress_ = std::clamp(secondsProcessed / kMaxAnalysisSeconds, 0.0, 1.0);
}

std::string StreamingAnalyzer::sectionIdAt(double timeSeconds) const
{
    const AnalysisSectionMarker* best = nullptr;
    for (const auto& s : sections_) {
        if (timeSeconds >= s.startSeconds && timeSeconds < s.endSeconds) {
            if (best == nullptr
                || (s.endSeconds - s.startSeconds) < (best->endSeconds - best->startSeconds))
                best = &s;
        }
    }
    return best != nullptr ? best->id : std::string {};
}

void StreamingAnalyzer::accumulateChunk(const float* mono, int frames, double timeOffsetSeconds)
{
    if (mono == nullptr || frames <= 0)
        return;

    const int maxFrames = static_cast<int>(kMaxAnalysisSeconds * sampleRate_);
    if (static_cast<int>(framesProcessed_) >= maxFrames)
        return;

    int usable = frames;
    if (static_cast<int>(framesProcessed_) + usable > maxFrames)
        usable = maxFrames - static_cast<int>(framesProcessed_);
    if (usable <= 0)
        return;

    for (int i = 0; i < usable; ++i) {
        const double x = double(mono[i]);
        sumSquares_ += x * x;
        peakAbs_ = std::max(peakAbs_, std::abs(x));
        overlapBuffer_.push_back(mono[i]);
    }
    framesProcessed_ += static_cast<std::uint64_t>(usable);
    totalAnalysisFrames_ += usable;

    // Transient / onset pass on this chunk (kick + snare bands via crest).
    const auto onsets = SpectralAnalysis::countOnsets(
        mono,
        static_cast<std::size_t>(usable),
        sampleRate_,
        kKickMinSpacing,
        1.8,
        0.05);

    for (double localT : onsets) {
        const double t = timeOffsetSeconds + localT;
        if (t - lastKickTime_ < kKickMinSpacing && t - lastSnareTime_ < kSnareMinSpacing)
            continue;

        const int center = static_cast<int>(localT * sampleRate_);
        const int transientWin = std::min(windows_.transient.sizeFrames, usable);
        // Low fundamentals need a longer window than the transient hop size
        // (one period of 40 Hz ≈ 25 ms; transient-only windows often miss F0).
        const int lowWin = std::min(
            std::max(windows_.lowFreq.sizeFrames / 2, transientWin * 4),
            usable);
        const int startLow = std::clamp(center - lowWin / 8, 0, std::max(0, usable - 1));
        const int lenLow = std::min(lowWin, usable - startLow);
        const int startTr = std::clamp(center - transientWin / 4, 0, std::max(0, usable - 1));
        const int lenTr = std::min(transientWin, usable - startTr);
        if (lenLow < 64 || lenTr < 32)
            continue;

        const auto lows = SpectralAnalysis::findPeaksInBand(
            mono + startLow,
            static_cast<std::size_t>(lenLow),
            sampleRate_,
            30.0,
            120.0,
            3);
        const auto bodies = SpectralAnalysis::findPeaksInBand(
            mono + startLow,
            static_cast<std::size_t>(lenLow),
            sampleRate_,
            80.0,
            250.0,
            2);
        const auto clicks = SpectralAnalysis::findPeaksInBand(
            mono + startTr,
            static_cast<std::size_t>(lenTr),
            sampleRate_,
            1'500.0,
            6'000.0,
            2);

        const double pk = peakDb(mono + startTr, lenTr);
        const double rms = rmsDb(mono + startTr, lenTr);
        const double crest = pk - rms;

        // Kick: strong low fundamental + moderate crest. Snare: weaker sub, higher crack.
        const bool kickLike = !lows.empty() && lows.front().magnitudeDb > -50.0 && crest < 18.0;
        const bool snareLike = !clicks.empty()
            && clicks.front().magnitudeDb > -40.0
            && clicks.front().prominenceDb > 2.0
            && (lows.empty() || lows.front().magnitudeDb < -40.0);

        if (kickLike && (t - lastKickTime_) >= kKickMinSpacing) {
            KickEvent ev;
            ev.timeSeconds = t;
            ev.fundamentalHz = lows.empty() ? 0.0 : lows.front().frequencyHz;
            ev.bodyHz = bodies.empty() ? ev.fundamentalHz : bodies.front().frequencyHz;
            ev.clickHz = clicks.empty() ? 0.0 : clicks.front().frequencyHz;
            ev.truePeakDb = pk;
            ev.crestDb = crest;
            ev.confidence = std::clamp(
                0.35 + 0.02 * std::max(0.0, lows.front().prominenceDb) + 0.01 * std::max(0.0, crest),
                0.0,
                1.0);
            ev.sectionId = sectionIdAt(t);
            kickEvents_.push_back(ev);
            lastKickTime_ = t;
            if (ev.fundamentalHz > 0.0)
                fundamentalSamples_.push_back(ev.fundamentalHz);
            for (const auto& p : lows)
                peakCandidates_.push_back(p);
            pushActivity(
                activityKick_,
                static_cast<int>(t * sampleRate_),
                true);
        } else if (snareLike && (t - lastSnareTime_) >= kSnareMinSpacing) {
            KickEvent ev;
            ev.timeSeconds = t;
            ev.fundamentalHz = bodies.empty() ? 0.0 : bodies.front().frequencyHz;
            ev.bodyHz = bodies.empty() ? 180.0 : bodies.front().frequencyHz;
            ev.clickHz = clicks.empty() ? 0.0 : clicks.front().frequencyHz;
            ev.truePeakDb = pk;
            ev.crestDb = crest;
            ev.confidence = std::clamp(
                0.30 + 0.02 * (clicks.empty() ? 0.0 : clicks.front().prominenceDb),
                0.0,
                1.0);
            ev.sectionId = sectionIdAt(t);
            snareEvents_.push_back(ev);
            lastSnareTime_ = t;
            pushActivity(
                activitySnare_,
                static_cast<int>(t * sampleRate_),
                true);
        }
    }

    // Low-freq / bass occupancy on hop windows from overlap buffer.
    const int lowSize = windows_.lowFreq.sizeFrames;
    const int lowHop = std::max(1, windows_.lowFreq.hopFrames);
    while (static_cast<int>(overlapBuffer_.size()) >= lowSize) {
        const float* win = overlapBuffer_.data();
        for (std::size_t b = 0; b < bassBinHz_.size(); ++b) {
            const double mag = SpectralAnalysis::toneMagnitudeDb(
                win,
                static_cast<std::size_t>(lowSize),
                sampleRate_,
                bassBinHz_[b]);
            // Convert dB to linear-ish energy for averaging, store as sum of dB later.
            bassEnergySum_[b] += mag;
            bassEnergyCount_[b] += 1;
        }

        // Presence / fizz band energy proxies.
        const double presence = SpectralAnalysis::toneMagnitudeDb(
            win, static_cast<std::size_t>(lowSize), sampleRate_, 3'500.0);
        const double fizz = SpectralAnalysis::toneMagnitudeDb(
            win, static_cast<std::size_t>(lowSize), sampleRate_, 9'000.0);
        const double lowMid = SpectralAnalysis::toneMagnitudeDb(
            win, static_cast<std::size_t>(lowSize), sampleRate_, 350.0);
        presenceEnergySum_ += presence;
        fizzEnergySum_ += fizz;
        lowMidEnergySum_ += lowMid;

        const double winRms = rmsDb(win, lowSize);
        if (winRms > -50.0)
            ++activeFrames_;

        const double tCentre = double(framesProcessed_ - overlapBuffer_.size() + lowSize / 2)
            / sampleRate_;
        const int frameIdx = static_cast<int>(tCentre * sampleRate_);
        pushActivity(activityPresence_, frameIdx, presence > -45.0);
        pushActivity(activityFizz_, frameIdx, fizz > -48.0);

        overlapBuffer_.erase(
            overlapBuffer_.begin(),
            overlapBuffer_.begin() + lowHop);
    }
}

void StreamingAnalyzer::processChunk(const float* mono, int frames)
{
    if (isCancelled() || mono == nullptr || frames <= 0)
        return;
    const double t0 = double(framesProcessed_) / sampleRate_;
    accumulateChunk(mono, frames, t0);
    updateProgress(double(framesProcessed_) / sampleRate_, kMaxAnalysisSeconds);
}

void StreamingAnalyzer::rebuildBassOccupancy()
{
    // Filled in finalizeProfiles.
}

void StreamingAnalyzer::buildActivityMasks(StreamingAnalysisResult& out)
{
    auto append = [&](std::vector<ActivityMask>&& masks) {
        out.masks.insert(out.masks.end(), masks.begin(), masks.end());
    };
    append(collapseActivity(activityKick_, "kick", sampleRate_, 0.7));
    append(collapseActivity(activitySnare_, "snare", sampleRate_, 0.65));
    append(collapseActivity(activityPresence_, "presence", sampleRate_, 0.55));
    append(collapseActivity(activityFizz_, "fizz", sampleRate_, 0.5));
}

void StreamingAnalyzer::buildSectionSummaries(StreamingAnalysisResult& out)
{
    if (sections_.empty())
        return;

    for (const auto& section : sections_) {
        SectionSummary summary;
        summary.sectionId = section.id;
        std::vector<double> fund;
        for (const auto& k : out.kickEvents) {
            if (k.timeSeconds >= section.startSeconds && k.timeSeconds < section.endSeconds) {
                ++summary.kickEventCount;
                if (k.fundamentalHz > 0.0)
                    fund.push_back(k.fundamentalHz);
            }
        }
        for (const auto& s : out.snareEvents) {
            if (s.timeSeconds >= section.startSeconds && s.timeSeconds < section.endSeconds)
                ++summary.snareEventCount;
        }
        summary.medianFundamentalHz = SpectralAnalysis::median(fund);
        summary.meanRmsDb = out.metrics.rmsDbfs;
        out.sectionSummaries.push_back(summary);
    }
}

void StreamingAnalyzer::finalizeProfiles(StreamingAnalysisResult& out)
{
    out.bass.binHz = bassBinHz_;
    out.bass.energyDb.assign(bassBinHz_.size(), -120.0);
    double bestDb = -1.0e9;
    double bestHz = 0.0;
    double subSum = 0.0;
    int subCount = 0;
    for (std::size_t i = 0; i < bassBinHz_.size(); ++i) {
        if (bassEnergyCount_[i] > 0)
            out.bass.energyDb[i] = bassEnergySum_[i] / double(bassEnergyCount_[i]);
        if (out.bass.energyDb[i] > bestDb) {
            bestDb = out.bass.energyDb[i];
            bestHz = bassBinHz_[i];
        }
        if (bassBinHz_[i] <= 60.0 && bassEnergyCount_[i] > 0) {
            subSum += out.bass.energyDb[i];
            ++subCount;
        }
    }
    out.bass.stableFundamentalHz = SpectralAnalysis::median(fundamentalSamples_);
    if (out.bass.stableFundamentalHz <= 0.0)
        out.bass.stableFundamentalHz = bestHz;
    out.bass.subOccupancyDb = subCount > 0 ? subSum / double(subCount) : -120.0;
    {
        std::ostringstream oss;
        oss << "Bass occupancy " << kBassLowHz << "-" << kBassHighHz << " Hz, "
            << kBassBinCount << " bins; median F0=" << out.bass.stableFundamentalHz
            << " Hz; subOcc=" << out.bass.subOccupancyDb << " dB";
        out.bass.evidence = oss.str();
    }

    // Kick low profile from events + peaks.
    out.kickLow.eventCount = static_cast<int>(out.kickEvents.size());
    out.kickLow.fundamentalCandidates = out.spectralPeakCandidates;
    if (out.kickLow.fundamentalCandidates.size() > 5)
        out.kickLow.fundamentalCandidates.resize(5);
    out.kickLow.dominantLowHz = out.bass.stableFundamentalHz;
    if (!out.kickEvents.empty()) {
        std::vector<double> bodies;
        std::vector<double> clicks;
        for (const auto& e : out.kickEvents) {
            if (e.bodyHz > 0.0)
                bodies.push_back(e.bodyHz);
            if (e.clickHz > 0.0)
                clicks.push_back(e.clickHz);
        }
        out.kickLow.bodyHz = SpectralAnalysis::median(bodies);
        out.kickLow.clickHz = SpectralAnalysis::median(clicks);
        double confSum = 0.0;
        for (const auto& e : out.kickEvents)
            confSum += e.confidence;
        out.kickLow.stabilityScore = std::clamp(
            confSum / double(out.kickEvents.size()),
            0.0,
            1.0);
    }
    out.kickLow.evidence = "Streaming kick events + lowFreq Goertzel occupancy";

    out.bassLow.dominantLowHz = bestHz;
    // Build peaks from occupancy histogram.
    out.bassLow.fundamentalCandidates.clear();
    for (std::size_t i = 1; i + 1 < out.bass.energyDb.size(); ++i) {
        if (out.bass.energyDb[i] > out.bass.energyDb[i - 1]
            && out.bass.energyDb[i] > out.bass.energyDb[i + 1]) {
            SpectralPeak p;
            p.frequencyHz = out.bass.binHz[i];
            p.magnitudeDb = out.bass.energyDb[i];
            p.prominenceDb = out.bass.energyDb[i]
                - 0.5 * (out.bass.energyDb[i - 1] + out.bass.energyDb[i + 1]);
            p.bandwidthHz = (kBassHighHz - kBassLowHz) / double(kBassBinCount);
            out.bassLow.fundamentalCandidates.push_back(p);
        }
    }
    std::sort(
        out.bassLow.fundamentalCandidates.begin(),
        out.bassLow.fundamentalCandidates.end(),
        [](const auto& a, const auto& b) { return a.prominenceDb > b.prominenceDb; });
    if (out.bassLow.fundamentalCandidates.size() > 5)
        out.bassLow.fundamentalCandidates.resize(5);
    out.bassLow.bodyHz = bestHz;
    out.bassLow.stabilityScore = out.kickLow.stabilityScore;
    out.bassLow.eventCount = out.kickLow.eventCount;
    out.bassLow.evidence = out.bass.evidence;

    const double duration = double(framesProcessed_) / sampleRate_;
    const int hops = std::max(1, totalAnalysisFrames_ / std::max(1, windows_.lowFreq.hopFrames));
    out.guitar.longTermRmsDb = out.metrics.rmsDbfs;
    out.guitar.presenceDb = presenceEnergySum_ / double(hops);
    out.guitar.fizzEnergyDb = fizzEnergySum_ / double(hops);
    out.guitar.lowMidBuildDb = lowMidEnergySum_ / double(hops);
    out.guitar.transientDensityHz = duration > 0.0
        ? double(out.kickEvents.size() + out.snareEvents.size()) / duration
        : 0.0;
    out.guitar.harshLikely = out.guitar.presenceDb > -35.0;
    out.guitar.fizzLikely = out.guitar.fizzEnergyDb > -40.0;
    out.guitar.mudLikely = out.guitar.lowMidBuildDb > -30.0;
    out.guitar.evidence = "Streaming presence/fizz/low-mid hop averages";

    out.vocal.activityRatio = totalAnalysisFrames_ > 0
        ? double(activeFrames_) / double(std::max(1, hops))
        : 0.0;
    out.vocal.presenceCentroidHz = 3'500.0;
    out.vocal.sibilanceHz = 7'500.0;
    out.vocal.sibilanceEnergyDb = out.guitar.fizzEnergyDb;
    out.vocal.crestDb = out.metrics.crestFactorDb;
    out.vocal.needsRide = out.vocal.activityRatio > 0.15 && out.metrics.crestFactorDb > 10.0;
    out.vocal.needsDeEss = out.vocal.sibilanceEnergyDb > -42.0;
    out.vocal.evidence = "Streaming activity + presence proxies";

    out.snare.eventRateHz = duration > 0.0 ? double(out.snareEvents.size()) / duration : 0.0;
    if (!out.snareEvents.empty()) {
        std::vector<double> cracks;
        std::vector<double> bodies;
        for (const auto& e : out.snareEvents) {
            if (e.clickHz > 0.0)
                cracks.push_back(e.clickHz);
            if (e.bodyHz > 0.0)
                bodies.push_back(e.bodyHz);
        }
        out.snare.crackHz = SpectralAnalysis::median(cracks);
        out.snare.bodyHz = SpectralAnalysis::median(bodies);
        out.snare.crackEnergyDb = out.snareEvents.front().truePeakDb;
    }
    out.snare.evidence = "Streaming snare-like onsets";
}

StreamingAnalysisResult StreamingAnalyzer::finalize()
{
    StreamingAnalysisResult out;
    out.cancelled = isCancelled();
    out.truncated = double(framesProcessed_) >= kMaxAnalysisSeconds * sampleRate_;
    out.cacheKey = cacheKey();
    out.progress = out.cancelled ? progress_ : 1.0;
    progress_ = out.progress;

    out.kickEvents = kickEvents_;
    out.snareEvents = snareEvents_;
    out.spectralPeakCandidates = peakCandidates_;

    const double duration = sampleRate_ > 0.0 ? double(framesProcessed_) / sampleRate_ : 0.0;
    out.metrics.durationSeconds = duration;
    out.metrics.sampleRate = static_cast<int>(sampleRate_);
    out.metrics.channels = 1;
    if (framesProcessed_ > 0) {
        const double rms = std::sqrt(sumSquares_ / double(framesProcessed_));
        out.metrics.rmsDbfs = 20.0 * std::log10(std::max(1.0e-12, rms));
        out.metrics.samplePeakDbfs = 20.0 * std::log10(std::max(1.0e-12, peakAbs_));
        out.metrics.crestFactorDb = out.metrics.samplePeakDbfs - out.metrics.rmsDbfs;
        out.metrics.estimatedLoudnessDb = out.metrics.rmsDbfs;
        out.metrics.estimatedLoudnessIsValid = true;
        out.metrics.transientDensityHz = duration > 0.0
            ? double(kickEvents_.size() + snareEvents_.size()) / duration
            : 0.0;
    }

    rebuildBassOccupancy();
    finalizeProfiles(out);
    buildActivityMasks(out);
    buildSectionSummaries(out);
    return out;
}

StreamingAnalysisResult StreamingAnalyzer::analyze(ReadChunkFn reader, double sampleRate)
{
    begin(sampleRate);
    StreamingAnalysisResult cancelled;
    if (!reader) {
        cancelled.cacheKey = cacheKey();
        cancelled.cancelled = true;
        return cancelled;
    }

    std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames));
    while (!isCancelled()) {
        if (double(framesProcessed_) / sampleRate_ >= kMaxAnalysisSeconds)
            break;
        int framesRead = 0;
        const bool ok = reader(chunk.data(), kChunkFrames, framesRead);
        if (!ok || framesRead <= 0)
            break;
        processChunk(chunk.data(), framesRead);
    }
    return finalize();
}

StreamingAnalysisResult StreamingAnalyzer::analyzeBuffer(
    const std::vector<float>& mono,
    double sampleRate)
{
    begin(sampleRate);
    const int maxFrames = static_cast<int>(kMaxAnalysisSeconds * sampleRate_);
    int offset = 0;
    const int total = std::min(static_cast<int>(mono.size()), maxFrames);
    while (offset < total && !isCancelled()) {
        const int n = std::min(kChunkFrames, total - offset);
        processChunk(mono.data() + offset, n);
        offset += n;
        updateProgress(double(offset) / sampleRate_, double(total) / sampleRate_);
    }
    return finalize();
}

} // namespace mastering::analysis
