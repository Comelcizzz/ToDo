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
    case MetricAvailability::stale: return "stale";
    case MetricAvailability::degraded: return "degraded";
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
    if (channelCount <= 2)
        return 1.0;
    if (channel == 3)
        return 0.0;
    if (channel == 4 || channel == 5)
        return 1.41;
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
    maximumChannels_ = std::clamp(maximumChannels, 1, 8);
    maximumBlockSize_ = std::max(1, maximumBlockSize);
    blockFrames_ = static_cast<std::size_t>(std::max<long>(1, std::lround(sampleRate_ * 0.4)));
    hopFrames_ = std::max<std::size_t>(1, blockFrames_ / 4);
    shortTermFrames_ = static_cast<std::size_t>(std::max<long>(1, std::lround(sampleRate_ * 3.0)));
    hopsPerBlock_ = std::max<std::size_t>(1, blockFrames_ / hopFrames_);

    shortTermEnergyRing_.assign(shortTermFrames_, 0.0);
    hopEnergyRing_.assign(hopsPerBlock_, 0.0);
    constexpr std::size_t kMaxBlocks = 6 * 3600 * 10;
    blockMeanSquares_.assign(kMaxBlocks, 0.0);
    lraShortTermScratch_.assign(kMaxBlocks, 0.0);
    lraGatedScratch_.assign(kMaxBlocks, 0.0);
    blockCount_ = 0;
    blockWrite_ = 0;

    configureFilters();
    reset();
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
    truePeakSamplesSeen_ = 0;
    shortTermWrite_ = 0;
    shortTermFilled_ = 0;
    shortTermEnergySum_ = 0.0;
    blockCount_ = 0;
    blockWrite_ = 0;
    lastChannelCount_ = 0;
    droppedAnalysisFrames_ = 0;
    reading_ = {};
    reading_.momentaryState = MetricAvailability::stale;
    reading_.shortTermState = MetricAvailability::stale;
    reading_.integratedState = MetricAvailability::stale;
    reading_.loudnessRangeState = MetricAvailability::stale;
    reading_.truePeakState = MetricAvailability::stale;
    reading_.samplePeakState = MetricAvailability::stale;
    for (int channel = 0; channel < maximumChannels_; ++channel) {
        channels_[static_cast<std::size_t>(channel)].shelf.reset();
        channels_[static_cast<std::size_t>(channel)].highPass.reset();
        truePeakHistory_[static_cast<std::size_t>(channel)].fill(0.0);
        channels_[static_cast<std::size_t>(channel)].weight =
            channelWeight(channel, maximumChannels_);
    }
    std::fill(shortTermEnergyRing_.begin(), shortTermEnergyRing_.end(), 0.0);
    std::fill(hopEnergyRing_.begin(), hopEnergyRing_.end(), 0.0);
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

void LoudnessMeter::processTruePeak(
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
            samplePeak_ = std::max(samplePeak_, std::abs(history[0]));

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
    if (!std::isfinite(samplePeak_))
        samplePeak_ = 0.0;
    if (!std::isfinite(truePeak_))
        truePeak_ = 0.0;
}

void LoudnessMeter::pushBlockMeanSquare(double meanSquare) noexcept
{
    if (blockMeanSquares_.empty())
        return;
    blockMeanSquares_[blockWrite_] = meanSquare;
    blockWrite_ = (blockWrite_ + 1) % blockMeanSquares_.size();
    if (blockCount_ < blockMeanSquares_.size())
        ++blockCount_;

    reading_.momentaryLufs = powerToLufs(meanSquare);
    reading_.momentaryValid = true;
    reading_.momentaryState = MetricAvailability::valid;
    recomputeIntegrated(false);
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

    const auto size = blockMeanSquares_.size();
    const auto start = blockCount_ < size ? std::size_t {0} : blockWrite_;

    double ungatedSum = 0.0;
    std::size_t ungatedCount = 0;
    for (std::size_t index = 0; index < blockCount_; ++index) {
        const auto power = blockMeanSquares_[(start + index) % size];
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
        const auto power = blockMeanSquares_[(start + index) % size];
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
    reading_.integratedState = finalized
        ? MetricAvailability::valid
        : MetricAvailability::provisional;
}

void LoudnessMeter::recomputeLoudnessRange() noexcept
{
    // PARTIAL vs EBU Tech 3342: uses short-term approximation from integrated blocks.
    if (blockCount_ < 2 || lraShortTermScratch_.empty()) {
        reading_.loudnessRangeValid = false;
        reading_.loudnessRangeLu = 0.0;
        reading_.loudnessRangeState = MetricAvailability::unavailable;
        return;
    }

    const auto size = blockMeanSquares_.size();
    const auto start = blockCount_ < size ? std::size_t {0} : blockWrite_;
    const auto shortTermBlocks = std::max<std::size_t>(1, hopsPerBlock_ * 7 + hopsPerBlock_ / 2);

    std::size_t shortTermCount = 0;
    for (std::size_t index = 0; index < blockCount_; ++index) {
        const auto begin = index + 1 >= shortTermBlocks ? index + 1 - shortTermBlocks : 0;
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t block = begin; block <= index; ++block) {
            sum += blockMeanSquares_[(start + block) % size];
            ++count;
        }
        lraShortTermScratch_[shortTermCount++] = powerToLufs(sum / static_cast<double>(count));
    }

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
        reading_.loudnessRangeState = MetricAvailability::unavailable;
        return;
    }
    std::sort(lraGatedScratch_.begin(), lraGatedScratch_.begin() + static_cast<std::ptrdiff_t>(gatedCount));
    const auto lowerIndex = static_cast<std::size_t>(0.10 * static_cast<double>(gatedCount - 1));
    const auto upperIndex = static_cast<std::size_t>(0.95 * static_cast<double>(gatedCount - 1));
    reading_.loudnessRangeLu = lraGatedScratch_[upperIndex] - lraGatedScratch_[lowerIndex];
    reading_.loudnessRangeValid = true;
    reading_.loudnessRangeState = MetricAvailability::valid;
}

void LoudnessMeter::refreshAvailabilityFlags() noexcept
{
    reading_.framesProcessed = totalFrames_;
    reading_.droppedAnalysisFrames = droppedAnalysisFrames_;
    reading_.samplePeakLinear = samplePeak_;
    reading_.truePeakLinear = truePeak_;

    if (totalFrames_ == 0) {
        reading_.samplePeakState = MetricAvailability::unavailable;
        reading_.truePeakState = MetricAvailability::unavailable;
    } else {
        reading_.samplePeakState = droppedAnalysisFrames_ > 0
            ? MetricAvailability::degraded
            : MetricAvailability::valid;
        if (truePeakSamplesSeen_ < static_cast<std::size_t>(kTruePeakWarmup)) {
            reading_.truePeakState = MetricAvailability::warmingUp;
            reading_.truePeakValid = false;
        } else {
            reading_.truePeakValid = truePeakReady_;
            reading_.truePeakState = reading_.truePeakValid
                ? (droppedAnalysisFrames_ > 0
                       ? MetricAvailability::degraded
                       : MetricAvailability::valid)
                : MetricAvailability::unavailable;
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

    if (!finalized_ && reading_.integratedValid)
        reading_.integratedProvisional = true;
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

    channelCount_ = std::min(channelCount, maximumChannels_);
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

    processTruePeak(channels, channelCount_, sampleCount);

    for (int sample = 0; sample < sampleCount; ++sample) {
        double weightedPower = 0.0;
        for (int channel = 0; channel < channelCount_; ++channel) {
            auto input = static_cast<double>(channels[channel][sample]);
            if (!std::isfinite(input))
                input = 0.0;
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
                reading_.shortTermState = MetricAvailability::valid;
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
        }
        framesInHop_ = 0;
        hopEnergy_ = 0.0;
    }
    finalized_ = true;
    recomputeIntegrated(true);
    recomputeLoudnessRange();
    refreshAvailabilityFlags();
    reading_.integratedProvisional = false;
    if (reading_.integratedValid)
        reading_.integratedState = MetricAvailability::valid;
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
