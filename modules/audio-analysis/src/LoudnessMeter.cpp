#include "mastering/analysis/LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace mastering::analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSilenceLufs = -120.0;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;
constexpr double kLraRelativeGateLu = -20.0;

} // namespace

const char* metricAvailabilityToString(MetricAvailability state) noexcept
{
    switch (state) {
    case MetricAvailability::unavailable: return "unavailable";
    case MetricAvailability::warmingUp: return "warmingUp";
    case MetricAvailability::valid: return "valid";
    case MetricAvailability::provisional: return "provisional";
    case MetricAvailability::unverified: return "unverified";
    case MetricAvailability::stale: return "stale";
    case MetricAvailability::degraded: return "degraded";
    case MetricAvailability::invalidInput: return "invalidInput";
    }
    return "unavailable";
}

double LoudnessMeter::powerToLufs(double meanSquarePower) noexcept
{
    return meanSquarePower > 1.0e-20
        ? -0.691 + 10.0 * std::log10(meanSquarePower)
        : kSilenceLufs;
}

double LoudnessMeter::channelWeight(int channel, int channelCount) noexcept
{
    // M1A: mono/stereo only — surround (5.1) weights are intentionally unused.
    if (channelCount <= 2 && (channel == 0 || channel == 1))
        return 1.0;
    return 1.0;
}

void LoudnessMeter::configureFilters() noexcept
{
    // ITU-R BS.1770 K-weighting stage design equations (RBJ-style biquads with
    // published stage parameters used widely for BS.1770 implementations).
    const auto makeShelf = [this](ChannelState& state) {
        constexpr double f0 = 1'681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double q = 0.7071752369554196;
        const auto amplitude = std::pow(10.0, gainDb / 40.0);
        const auto omega = 2.0 * kPi * f0 / sampleRate_;
        const auto cosOmega = std::cos(omega);
        const auto sinOmega = std::sin(omega);
        const auto alpha = sinOmega / (2.0 * q);
        const auto beta = 2.0 * std::sqrt(amplitude) * alpha;
        const auto a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosOmega + beta;
        state.shelf = {
            amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega + beta) / a0,
            -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosOmega) / a0,
            amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega - beta) / a0,
            2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosOmega) / a0,
            ((amplitude + 1.0) - (amplitude - 1.0) * cosOmega - beta) / a0,
            0.0,
            0.0
        };
    };

    const auto makeHighPass = [this](ChannelState& state) {
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;
        const auto omega = 2.0 * kPi * f0 / sampleRate_;
        const auto alpha = std::sin(omega) / (2.0 * q);
        const auto cosOmega = std::cos(omega);
        const auto a0 = 1.0 + alpha;
        state.highPass = {
            (1.0 + cosOmega) * 0.5 / a0,
            -(1.0 + cosOmega) / a0,
            (1.0 + cosOmega) * 0.5 / a0,
            -2.0 * cosOmega / a0,
            (1.0 - alpha) / a0,
            0.0,
            0.0
        };
    };

    for (int channel = 0; channel < maximumChannels_; ++channel) {
        makeShelf(channels_[static_cast<std::size_t>(channel)]);
        makeHighPass(channels_[static_cast<std::size_t>(channel)]);
        channels_[static_cast<std::size_t>(channel)].weight =
            channelWeight(channel, maximumChannels_);
    }

    // 4× polyphase Hann-windowed sinc lowpass (cutoff π/4 in upsampled domain).
    // This is an Annex-2-*method* approximation, not ITU-published coefficient set.
    constexpr int prototypeLength = kTruePeakTaps * kTruePeakPhases;
    constexpr int half = prototypeLength / 2;
    for (int phase = 0; phase < kTruePeakPhases; ++phase) {
        double sum = 0.0;
        for (int tap = 0; tap < kTruePeakTaps; ++tap) {
            const auto index = tap * kTruePeakPhases + phase - half;
            double value = 0.0;
            if (index == 0) {
                value = static_cast<double>(kTruePeakPhases);
            } else {
                const auto x = static_cast<double>(index);
                value = std::sin(kPi * x / kTruePeakPhases) / (kPi * x / kTruePeakPhases);
            }
            const auto window = 0.5
                + 0.5 * std::cos(kPi * static_cast<double>(index) / static_cast<double>(half));
            const auto coeff = value * window;
            truePeakCoeffs_[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)] = coeff;
            sum += coeff;
        }
        // Normalize each phase so DC gain ≈ 1 (pass-through for constant signals).
        if (std::abs(sum) > 1.0e-12) {
            for (auto& coeff : truePeakCoeffs_[static_cast<std::size_t>(phase)])
                coeff /= sum;
        }
    }
    truePeakReady_ = true;
}

void LoudnessMeter::prepare(double sampleRate, int maximumChannels, int maximumBlockSize) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    // M1A officially supports mono and stereo only.
    maximumChannels_ = std::clamp(maximumChannels, 1, 2);
    maximumBlockSize_ = std::max(1, maximumBlockSize);
    blockFrames_ = static_cast<std::size_t>(std::max<long>(1, std::lround(sampleRate_ * 0.4)));
    hopFrames_ = std::max<std::size_t>(1, blockFrames_ / 4);
    shortTermFrames_ = static_cast<std::size_t>(std::max<long>(1, std::lround(sampleRate_ * 3.0)));
    hopsPerBlock_ = std::max<std::size_t>(1, blockFrames_ / hopFrames_);

    shortTermEnergyRing_.assign(shortTermFrames_, 0.0);
    hopEnergyRing_.assign(hopsPerBlock_, 0.0);

    // ≥ 6 hours at ~100 ms hop: 6 * 3600 * 10 blocks.
    // integratedCapacitySeconds ≈ blockCapacity_ * (hopFrames_ / sampleRate_) ≈ blockCapacity_ * 0.1.
    blockCapacity_ = 6 * 3600 * 10;
    blockMeanSquares_.assign(blockCapacity_, 0.0);
    shortTermLoudnessHistory_.assign(blockCapacity_, 0.0);
    lraShortTermScratch_.assign(blockCapacity_, 0.0);
    lraGatedScratch_.assign(blockCapacity_, 0.0);
    blockCount_ = 0;
    blockWrite_ = 0;
    shortTermHistoryCount_ = 0;
    shortTermHistoryWrite_ = 0;

    configureFilters();
    reset();

    reading_.integratedCapacitySeconds =
        static_cast<double>(blockCapacity_)
        * (static_cast<double>(hopFrames_) / sampleRate_);
}

void LoudnessMeter::reset() noexcept
{
    framesInHop_ = 0;
    hopEnergy_ = 0.0;
    hopsFilled_ = 0;
    hopWrite_ = 0;
    totalFrames_ = 0;
    samplePeak_ = 0.0;
    truePeak_ = 0.0;
    finalized_ = false;
    programmeCapacityExceeded_ = false;
    truePeakTailFlushed_ = false;
    sawInvalidInput_ = false;
    truePeakSamplesSeen_ = 0;
    shortTermWrite_ = 0;
    shortTermFilled_ = 0;
    shortTermEnergySum_ = 0.0;
    blockCount_ = 0;
    blockWrite_ = 0;
    shortTermHistoryCount_ = 0;
    shortTermHistoryWrite_ = 0;
    lastChannelCount_ = 0;
    droppedAnalysisFrames_ = 0;
    reading_ = {};
    reading_.momentaryState = MetricAvailability::stale;
    reading_.shortTermState = MetricAvailability::stale;
    reading_.integratedState = MetricAvailability::stale;
    reading_.loudnessRangeState = MetricAvailability::stale;
    reading_.truePeakState = MetricAvailability::stale;
    reading_.samplePeakState = MetricAvailability::stale;
    reading_.truePeakIsEstimate = true;
    reading_.integratedCapacitySeconds =
        sampleRate_ > 0.0
            ? static_cast<double>(blockCapacity_)
                * (static_cast<double>(hopFrames_) / sampleRate_)
            : 0.0;
    for (int channel = 0; channel < maximumChannels_; ++channel) {
        channels_[static_cast<std::size_t>(channel)].shelf.reset();
        channels_[static_cast<std::size_t>(channel)].highPass.reset();
        truePeakHistory_[static_cast<std::size_t>(channel)].fill(0.0);
        channels_[static_cast<std::size_t>(channel)].weight =
            channelWeight(channel, maximumChannels_);
    }
    std::fill(shortTermEnergyRing_.begin(), shortTermEnergyRing_.end(), 0.0);
    std::fill(hopEnergyRing_.begin(), hopEnergyRing_.end(), 0.0);
    std::fill(blockMeanSquares_.begin(), blockMeanSquares_.end(), 0.0);
    std::fill(shortTermLoudnessHistory_.begin(), shortTermLoudnessHistory_.end(), 0.0);
}

void LoudnessMeter::noteDroppedAnalysisFrames(std::uint64_t count) noexcept
{
    droppedAnalysisFrames_ += count;
    reading_.droppedAnalysisFrames = droppedAnalysisFrames_;
    if (count > 0) {
        if (reading_.momentaryState == MetricAvailability::valid)
            reading_.momentaryState = MetricAvailability::degraded;
        if (reading_.shortTermState == MetricAvailability::valid)
            reading_.shortTermState = MetricAvailability::degraded;
        if (reading_.integratedState == MetricAvailability::provisional
            || reading_.integratedState == MetricAvailability::valid) {
            reading_.integratedState = MetricAvailability::degraded;
        }
    }
}

void LoudnessMeter::accumulateSamplePeak(
    const float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    for (int sample = 0; sample < sampleCount; ++sample) {
        for (int channel = 0; channel < channelCount; ++channel) {
            const auto value = static_cast<double>(channels[channel][sample]);
            if (!std::isfinite(value)) {
                sawInvalidInput_ = true;
                continue;
            }
            samplePeak_ = std::max(samplePeak_, std::abs(value));
        }
    }
    if (!std::isfinite(samplePeak_))
        samplePeak_ = 0.0;
}

void LoudnessMeter::reconstructTruePeak(
    const float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (!truePeakReady_)
        return;

    for (int sample = 0; sample < sampleCount; ++sample) {
        for (int channel = 0; channel < channelCount; ++channel) {
            auto& history = truePeakHistory_[static_cast<std::size_t>(channel)];
            for (int tap = kTruePeakTaps - 1; tap > 0; --tap)
                history[static_cast<std::size_t>(tap)] = history[static_cast<std::size_t>(tap - 1)];
            history[0] = static_cast<double>(channels[channel][sample]);
            if (!std::isfinite(history[0]))
                history[0] = 0.0;

            for (int phase = 0; phase < kTruePeakPhases; ++phase) {
                double interpolated = 0.0;
                const auto& coeffs = truePeakCoeffs_[static_cast<std::size_t>(phase)];
                for (int tap = 0; tap < kTruePeakTaps; ++tap)
                    interpolated += coeffs[static_cast<std::size_t>(tap)]
                        * history[static_cast<std::size_t>(tap)];
                truePeak_ = std::max(truePeak_, std::abs(interpolated));
            }
        }
        ++truePeakSamplesSeen_;
    }
    if (!std::isfinite(truePeak_))
        truePeak_ = 0.0;
}

void LoudnessMeter::flushTruePeakTail() noexcept
{
    if (truePeakTailFlushed_ || !truePeakReady_)
        return;
    truePeakTailFlushed_ = true;

    const int channelsToFlush = std::clamp(
        channelCount_ > 0 ? channelCount_ : maximumChannels_,
        1,
        maximumChannels_);

    float silence[kTruePeakTaps] {};
    const float* pointers[2] {silence, silence};
    reconstructTruePeak(pointers, channelsToFlush, kTruePeakTaps);
}

void LoudnessMeter::pushBlockMeanSquare(double meanSquare) noexcept
{
    reading_.momentaryLufs = powerToLufs(meanSquare);
    reading_.momentaryValid = true;
    reading_.momentaryState = droppedAnalysisFrames_ > 0
        ? MetricAvailability::degraded
        : MetricAvailability::valid;

    if (blockCapacity_ == 0 || blockMeanSquares_.empty()) {
        recomputeIntegrated(finalized_);
        return;
    }

    if (blockCount_ >= blockCapacity_) {
        // No silent sliding — stop accepting new integrated blocks for programme integrity.
        programmeCapacityExceeded_ = true;
        reading_.programmeCapacityExceeded = true;
        if (reading_.integratedState == MetricAvailability::provisional
            || reading_.integratedState == MetricAvailability::valid
            || reading_.integratedState == MetricAvailability::degraded) {
            reading_.integratedState = MetricAvailability::degraded;
        } else if (reading_.integratedValid) {
            reading_.integratedState = MetricAvailability::degraded;
        }
        return;
    }

    blockMeanSquares_[blockWrite_] = meanSquare;
    ++blockWrite_;
    ++blockCount_;
    recomputeIntegrated(finalized_);
}

void LoudnessMeter::pushShortTermLoudnessSample() noexcept
{
    if (!reading_.shortTermValid || shortTermLoudnessHistory_.empty())
        return;
    if (shortTermHistoryCount_ >= shortTermLoudnessHistory_.size())
        return; // stop without sliding — preserve LRA programme integrity

    shortTermLoudnessHistory_[shortTermHistoryWrite_] = reading_.shortTermLufs;
    ++shortTermHistoryWrite_;
    ++shortTermHistoryCount_;
}

void LoudnessMeter::emitHop() noexcept
{
    if (hopEnergyRing_.empty() || hopsPerBlock_ == 0)
        return;

    hopEnergyRing_[hopWrite_] = hopEnergy_ / static_cast<double>(hopFrames_);
    hopWrite_ = (hopWrite_ + 1) % hopsPerBlock_;
    if (hopsFilled_ < hopsPerBlock_)
        ++hopsFilled_;

    hopEnergy_ = 0.0;
    framesInHop_ = 0;

    if (hopsFilled_ < hopsPerBlock_)
        return;

    double sum = 0.0;
    for (std::size_t index = 0; index < hopsPerBlock_; ++index)
        sum += hopEnergyRing_[index];
    pushBlockMeanSquare(sum / static_cast<double>(hopsPerBlock_));

    if (reading_.shortTermValid)
        pushShortTermLoudnessSample();
}

void LoudnessMeter::recomputeIntegrated(bool finalized) noexcept
{
    if (blockCount_ == 0) {
        reading_.integratedValid = false;
        reading_.integratedProvisional = false;
        reading_.integratedLufs = kSilenceLufs;
        reading_.integratedState = totalFrames_ == 0
            ? MetricAvailability::unavailable
            : MetricAvailability::warmingUp;
        return;
    }

    // Linear fill only (no overwrite) — blocks occupy [0, blockCount_).
    double ungatedSum = 0.0;
    std::size_t ungatedCount = 0;
    for (std::size_t index = 0; index < blockCount_; ++index) {
        const auto power = blockMeanSquares_[index];
        if (powerToLufs(power) >= kAbsoluteGateLufs) {
            ungatedSum += power;
            ++ungatedCount;
        }
    }
    if (ungatedCount == 0) {
        reading_.integratedValid = false;
        reading_.integratedProvisional = false;
        reading_.integratedLufs = kSilenceLufs;
        reading_.integratedState = MetricAvailability::warmingUp;
        return;
    }

    const auto ungatedMean = ungatedSum / static_cast<double>(ungatedCount);
    const auto relativeGate = powerToLufs(ungatedMean) + kRelativeGateLu;

    double gatedSum = 0.0;
    std::size_t gatedCount = 0;
    for (std::size_t index = 0; index < blockCount_; ++index) {
        const auto power = blockMeanSquares_[index];
        if (powerToLufs(power) >= relativeGate) {
            gatedSum += power;
            ++gatedCount;
        }
    }
    if (gatedCount == 0) {
        reading_.integratedValid = false;
        reading_.integratedProvisional = false;
        reading_.integratedLufs = kSilenceLufs;
        reading_.integratedState = MetricAvailability::warmingUp;
        return;
    }

    reading_.integratedLufs = powerToLufs(gatedSum / static_cast<double>(gatedCount));
    reading_.integratedValid = true;
    reading_.integratedProvisional = !finalized;
    if (programmeCapacityExceeded_ || droppedAnalysisFrames_ > 0) {
        reading_.integratedState = MetricAvailability::degraded;
    } else {
        reading_.integratedState = finalized
            ? MetricAvailability::valid
            : MetricAvailability::provisional;
    }
}

void LoudnessMeter::recomputeLoudnessRange() noexcept
{
    // EBU Tech 3342 LRA from short-term loudness history (not block approximation).
    // Official LRA status remains PARTIAL until vectors pass → state=unverified.
    if (shortTermHistoryCount_ < 2 || lraShortTermScratch_.empty() || lraGatedScratch_.empty()) {
        reading_.loudnessRangeValid = false;
        reading_.loudnessRangeLu = 0.0;
        reading_.loudnessRangeState = MetricAvailability::unavailable;
        return;
    }

    const auto shortTermCount = shortTermHistoryCount_;
    for (std::size_t index = 0; index < shortTermCount; ++index)
        lraShortTermScratch_[index] = shortTermLoudnessHistory_[index];

    double absPowerSum = 0.0;
    std::size_t absCount = 0;
    for (std::size_t index = 0; index < shortTermCount; ++index) {
        const auto value = lraShortTermScratch_[index];
        if (value >= kAbsoluteGateLufs) {
            absPowerSum += std::pow(10.0, (value + 0.691) / 10.0);
            ++absCount;
        }
    }
    if (absCount == 0) {
        reading_.loudnessRangeValid = false;
        reading_.loudnessRangeLu = 0.0;
        reading_.loudnessRangeState = MetricAvailability::unavailable;
        return;
    }
    const auto absLoudness = powerToLufs(absPowerSum / static_cast<double>(absCount));
    const auto relativeGate = absLoudness + kLraRelativeGateLu;

    std::size_t gatedCount = 0;
    for (std::size_t index = 0; index < shortTermCount; ++index) {
        if (lraShortTermScratch_[index] >= relativeGate)
            lraGatedScratch_[gatedCount++] = lraShortTermScratch_[index];
    }
    if (gatedCount < 2) {
        reading_.loudnessRangeValid = false;
        reading_.loudnessRangeLu = 0.0;
        reading_.loudnessRangeState = MetricAvailability::unavailable;
        return;
    }
    std::sort(lraGatedScratch_.begin(), lraGatedScratch_.begin() + static_cast<std::ptrdiff_t>(gatedCount));
    const auto lowerIndex = static_cast<std::size_t>(0.10 * static_cast<double>(gatedCount - 1));
    const auto upperIndex = static_cast<std::size_t>(0.95 * static_cast<double>(gatedCount - 1));
    reading_.loudnessRangeLu = lraGatedScratch_[upperIndex] - lraGatedScratch_[lowerIndex];
    reading_.loudnessRangeValid = true;
    reading_.loudnessRangeState = MetricAvailability::unverified;
}

void LoudnessMeter::refreshAvailabilityFlags() noexcept
{
    reading_.framesProcessed = totalFrames_;
    reading_.droppedAnalysisFrames = droppedAnalysisFrames_;
    reading_.samplePeakLinear = samplePeak_;
    reading_.truePeakLinear = truePeak_;
    reading_.programmeCapacityExceeded = programmeCapacityExceeded_;
    reading_.finalized = finalized_;
    reading_.truePeakIsEstimate = true;
    reading_.integratedCapacitySeconds =
        sampleRate_ > 0.0
            ? static_cast<double>(blockCapacity_)
                * (static_cast<double>(hopFrames_) / sampleRate_)
            : 0.0;

    if (totalFrames_ == 0) {
        reading_.samplePeakState = MetricAvailability::unavailable;
        reading_.truePeakState = MetricAvailability::unavailable;
        reading_.truePeakValid = false;
    } else {
        if (sawInvalidInput_) {
            reading_.samplePeakState = MetricAvailability::invalidInput;
        } else {
            reading_.samplePeakState = droppedAnalysisFrames_ > 0
                ? MetricAvailability::degraded
                : MetricAvailability::valid;
        }
        if (truePeakSamplesSeen_ < static_cast<std::size_t>(kTruePeakWarmup)) {
            reading_.truePeakState = MetricAvailability::warmingUp;
            reading_.truePeakValid = false;
        } else {
            // Numeric available; official TP vectors not passed → unverified estimate.
            reading_.truePeakValid = truePeakReady_;
            if (!reading_.truePeakValid) {
                reading_.truePeakState = MetricAvailability::unavailable;
            } else if (sawInvalidInput_) {
                reading_.truePeakState = MetricAvailability::invalidInput;
            } else if (droppedAnalysisFrames_ > 0) {
                reading_.truePeakState = MetricAvailability::degraded;
            } else {
                reading_.truePeakState = MetricAvailability::unverified;
            }
        }
    }

    if (totalFrames_ == 0) {
        reading_.momentaryState = MetricAvailability::unavailable;
        reading_.momentaryValid = false;
    } else if (totalFrames_ < blockFrames_) {
        reading_.momentaryState = MetricAvailability::warmingUp;
        reading_.momentaryValid = false;
    }

    if (totalFrames_ == 0) {
        reading_.shortTermState = MetricAvailability::unavailable;
        reading_.shortTermValid = false;
    } else if (shortTermFilled_ < shortTermFrames_) {
        reading_.shortTermState = MetricAvailability::warmingUp;
        reading_.shortTermValid = false;
    } else {
        reading_.shortTermValid = true;
        reading_.shortTermState = droppedAnalysisFrames_ > 0
            ? MetricAvailability::degraded
            : MetricAvailability::valid;
    }

    if (!finalized_ && reading_.integratedValid && !programmeCapacityExceeded_
        && droppedAnalysisFrames_ == 0) {
        reading_.integratedProvisional = true;
        reading_.integratedState = MetricAvailability::provisional;
    }

    if (programmeCapacityExceeded_ && reading_.integratedValid)
        reading_.integratedState = MetricAvailability::degraded;
}

void LoudnessMeter::process(
    const float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (channels == nullptr || channelCount <= 0 || sampleCount <= 0 || sampleRate_ <= 0.0)
        return;

    // If host delivers a larger block than prepared, clamp analysis (audio path is separate).
    if (sampleCount > maximumBlockSize_) {
        noteDroppedAnalysisFrames(static_cast<std::uint64_t>(sampleCount - maximumBlockSize_));
        sampleCount = maximumBlockSize_;
    }

    // M1A: ignore channels beyond stereo; count ignored channel-samples as dropped analysis.
    if (channelCount > maximumChannels_) {
        noteDroppedAnalysisFrames(static_cast<std::uint64_t>(channelCount - maximumChannels_)
            * static_cast<std::uint64_t>(sampleCount));
        channelCount = maximumChannels_;
    }

    channelCount_ = channelCount;
    if (lastChannelCount_ != 0 && lastChannelCount_ != channelCount_) {
        // Channel layout change: reset filter/TP history to avoid smearing.
        for (int channel = 0; channel < maximumChannels_; ++channel) {
            channels_[static_cast<std::size_t>(channel)].shelf.reset();
            channels_[static_cast<std::size_t>(channel)].highPass.reset();
            truePeakHistory_[static_cast<std::size_t>(channel)].fill(0.0);
        }
        truePeakSamplesSeen_ = 0;
    }
    lastChannelCount_ = channelCount_;

    for (int channel = 0; channel < channelCount_; ++channel)
        channels_[static_cast<std::size_t>(channel)].weight =
            channelWeight(channel, channelCount_);

    accumulateSamplePeak(channels, channelCount_, sampleCount);
    reconstructTruePeak(channels, channelCount_, sampleCount);

    for (int sample = 0; sample < sampleCount; ++sample) {
        double weightedPower = 0.0;
        for (int channel = 0; channel < channelCount_; ++channel) {
            auto input = static_cast<double>(channels[channel][sample]);
            if (!std::isfinite(input)) {
                sawInvalidInput_ = true;
                input = 0.0;
            }
            auto& state = channels_[static_cast<std::size_t>(channel)];
            const auto filtered = state.highPass.process(state.shelf.process(input));
            weightedPower += state.weight * filtered * filtered;
        }

        hopEnergy_ += weightedPower;
        ++framesInHop_;
        ++totalFrames_;

        if (!shortTermEnergyRing_.empty()) {
            if (shortTermFilled_ == shortTermFrames_)
                shortTermEnergySum_ -= shortTermEnergyRing_[shortTermWrite_];
            else
                ++shortTermFilled_;
            shortTermEnergyRing_[shortTermWrite_] = weightedPower;
            shortTermEnergySum_ += weightedPower;
            shortTermWrite_ = (shortTermWrite_ + 1) % shortTermFrames_;
            if (shortTermFilled_ == shortTermFrames_) {
                reading_.shortTermLufs =
                    powerToLufs(shortTermEnergySum_ / static_cast<double>(shortTermFrames_));
                reading_.shortTermValid = true;
                reading_.shortTermState = droppedAnalysisFrames_ > 0
                    ? MetricAvailability::degraded
                    : MetricAvailability::valid;
            }
        }

        if (framesInHop_ >= hopFrames_)
            emitHop();
    }

    refreshAvailabilityFlags();
    if (!std::isfinite(reading_.samplePeakLinear))
        reading_.samplePeakLinear = 0.0;
    if (!std::isfinite(reading_.truePeakLinear))
        reading_.truePeakLinear = 0.0;
    if (!std::isfinite(samplePeak_))
        samplePeak_ = 0.0;
    if (!std::isfinite(truePeak_))
        truePeak_ = 0.0;
}

void LoudnessMeter::finalize() noexcept
{
    if (finalized_) {
        reading_.finalized = true;
        reading_.integratedProvisional = false;
        reading_.programmeCapacityExceeded = programmeCapacityExceeded_;
        return;
    }

    if (framesInHop_ > 0) {
        hopEnergyRing_[hopWrite_] =
            hopEnergy_ / static_cast<double>(std::max<std::size_t>(1, framesInHop_));
        hopWrite_ = (hopWrite_ + 1) % std::max<std::size_t>(1, hopsPerBlock_);
        if (hopsFilled_ < hopsPerBlock_)
            ++hopsFilled_;
        if (hopsFilled_ >= hopsPerBlock_) {
            double sum = 0.0;
            for (std::size_t index = 0; index < hopsPerBlock_; ++index)
                sum += hopEnergyRing_[index];
            pushBlockMeanSquare(sum / static_cast<double>(hopsPerBlock_));
            if (reading_.shortTermValid)
                pushShortTermLoudnessSample();
        }
        framesInHop_ = 0;
        hopEnergy_ = 0.0;
    }

    flushTruePeakTail();
    finalized_ = true;
    recomputeIntegrated(true);
    recomputeLoudnessRange();
    refreshAvailabilityFlags();
    reading_.integratedProvisional = false;
    reading_.finalized = true;
    if (reading_.integratedValid) {
        reading_.integratedState = (programmeCapacityExceeded_ || droppedAnalysisFrames_ > 0)
            ? MetricAvailability::degraded
            : MetricAvailability::valid;
    }
}

LoudnessReading LoudnessMeter::snapshot() const noexcept
{
    return reading_;
}

double LoudnessMeter::measureKWeightMagnitudeDb(double frequencyHz) const noexcept
{
    if (sampleRate_ <= 0.0 || frequencyHz <= 0.0)
        return kSilenceLufs;

    // Steady-state complex response of shelf * high-pass for channel 0 coefficients.
    const auto& shelf = channels_[0].shelf;
    const auto& highPass = channels_[0].highPass;
    const auto omega = 2.0 * kPi * frequencyHz / sampleRate_;
    const auto zr = std::cos(omega);
    const auto zi = -std::sin(omega); // z^{-1}
    const auto z2r = zr * zr - zi * zi;
    const auto z2i = 2.0 * zr * zi;

    auto eval = [&](const Biquad& b) {
        const auto numR = b.b0 + b.b1 * zr + b.b2 * z2r;
        const auto numI = b.b1 * zi + b.b2 * z2i;
        const auto denR = 1.0 + b.a1 * zr + b.a2 * z2r;
        const auto denI = b.a1 * zi + b.a2 * z2i;
        const auto denMag2 = denR * denR + denI * denI;
        const auto outR = (numR * denR + numI * denI) / denMag2;
        const auto outI = (numI * denR - numR * denI) / denMag2;
        return std::pair {outR, outI};
    };

    const auto s = eval(shelf);
    const auto h = eval(highPass);
    const auto r = s.first * h.first - s.second * h.second;
    const auto i = s.first * h.second + s.second * h.first;
    const auto mag = std::sqrt(r * r + i * i);
    return mag > 1.0e-12 ? 20.0 * std::log10(mag) : kSilenceLufs;
}

void LoudnessMeter::dumpTruePeakCoefficients(std::vector<double>& interleavedPhases) const
{
    interleavedPhases.clear();
    interleavedPhases.reserve(static_cast<std::size_t>(kTruePeakPhases * kTruePeakTaps));
    for (int phase = 0; phase < kTruePeakPhases; ++phase)
        for (int tap = 0; tap < kTruePeakTaps; ++tap)
            interleavedPhases.push_back(
                truePeakCoeffs_[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)]);
}

} // namespace mastering::analysis
