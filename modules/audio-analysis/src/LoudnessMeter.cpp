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

    constexpr int half = kTruePeakTaps * kTruePeakPhases / 2;
    for (int phase = 0; phase < kTruePeakPhases; ++phase) {
        double sum = 0.0;
        for (int tap = 0; tap < kTruePeakTaps; ++tap) {
            const auto index = tap * kTruePeakPhases + phase - half;
            double value = index == 0
                ? 1.0
                : std::sin(kPi * static_cast<double>(index) / kTruePeakPhases)
                    / (kPi * static_cast<double>(index) / kTruePeakPhases);
            const auto window = 0.5
                + 0.5
                    * std::cos(
                        kPi * static_cast<double>(tap * kTruePeakPhases + phase - half)
                        / static_cast<double>(half));
            truePeakCoeffs_[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)] =
                value * window;
            sum += value * window;
        }
        if (std::abs(sum) > 1.0e-12) {
            for (auto& coeff : truePeakCoeffs_[static_cast<std::size_t>(phase)])
                coeff /= sum;
        }
    }
    truePeakReady_ = true;
}

void LoudnessMeter::prepare(double sampleRate, int maximumChannels) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maximumChannels_ = std::clamp(maximumChannels, 1, 8);
    blockFrames_ = static_cast<std::size_t>(std::max<long>(1, std::lround(sampleRate_ * 0.4)));
    hopFrames_ = std::max<std::size_t>(1, blockFrames_ / 4);
    shortTermFrames_ = static_cast<std::size_t>(std::max<long>(1, std::lround(sampleRate_ * 3.0)));
    hopsPerBlock_ = static_cast<std::size_t>(
        std::max<std::size_t>(1, blockFrames_ / hopFrames_));

    // Preallocate only — process()/emit must not grow storage.
    shortTermEnergyRing_.assign(shortTermFrames_, 0.0);
    hopEnergyRing_.assign(hopsPerBlock_, 0.0);
    // ~6 hours of 100 ms hops at any rate.
    constexpr std::size_t kMaxBlocks = 6 * 3600 * 10;
    blockMeanSquares_.assign(kMaxBlocks, 0.0);
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
    shortTermWrite_ = 0;
    shortTermFilled_ = 0;
    shortTermEnergySum_ = 0.0;
    blockCount_ = 0;
    blockWrite_ = 0;
    reading_ = {};
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
    }
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
    recomputeIntegrated();
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

void LoudnessMeter::recomputeIntegrated() noexcept
{
    if (blockCount_ == 0) {
        reading_.integratedValid = false;
        reading_.integratedLufs = kSilenceLufs;
        return;
    }

    const auto size = blockMeanSquares_.size();
    const auto start = blockCount_ < size
        ? std::size_t {0}
        : blockWrite_;

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
        reading_.integratedLufs = kSilenceLufs;
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
        reading_.integratedLufs = kSilenceLufs;
        return;
    }

    reading_.integratedLufs = powerToLufs(gatedSum / static_cast<double>(gatedCount));
    reading_.integratedValid = true;
}

void LoudnessMeter::recomputeLoudnessRange() noexcept
{
    if (blockCount_ < 2) {
        reading_.loudnessRangeValid = false;
        reading_.loudnessRangeLu = 0.0;
        return;
    }

    const auto size = blockMeanSquares_.size();
    const auto start = blockCount_ < size ? std::size_t {0} : blockWrite_;
    const auto shortTermBlocks = std::max<std::size_t>(1, hopsPerBlock_ * 7 + hopsPerBlock_ / 2);

    std::vector<double> shortTermLufs;
    shortTermLufs.reserve(blockCount_);
    for (std::size_t index = 0; index < blockCount_; ++index) {
        const auto begin = index + 1 >= shortTermBlocks ? index + 1 - shortTermBlocks : 0;
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t block = begin; block <= index; ++block) {
            sum += blockMeanSquares_[(start + block) % size];
            ++count;
        }
        shortTermLufs.push_back(powerToLufs(sum / static_cast<double>(count)));
    }

    double absPowerSum = 0.0;
    std::size_t absCount = 0;
    for (const auto value : shortTermLufs) {
        if (value >= kAbsoluteGateLufs) {
            absPowerSum += std::pow(10.0, (value + 0.691) / 10.0);
            ++absCount;
        }
    }
    if (absCount == 0) {
        reading_.loudnessRangeValid = false;
        return;
    }
    const auto absLoudness = powerToLufs(absPowerSum / static_cast<double>(absCount));
    const auto relativeGate = absLoudness + kLraRelativeGateLu;

    std::vector<double> gated;
    gated.reserve(shortTermLufs.size());
    for (const auto value : shortTermLufs) {
        if (value >= relativeGate)
            gated.push_back(value);
    }
    if (gated.size() < 2) {
        reading_.loudnessRangeValid = false;
        return;
    }
    std::sort(gated.begin(), gated.end());
    const auto lowerIndex = static_cast<std::size_t>(0.10 * static_cast<double>(gated.size() - 1));
    const auto upperIndex = static_cast<std::size_t>(0.95 * static_cast<double>(gated.size() - 1));
    reading_.loudnessRangeLu = gated[upperIndex] - gated[lowerIndex];
    reading_.loudnessRangeValid = true;
}

void LoudnessMeter::process(
    const float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (channels == nullptr || channelCount <= 0 || sampleCount <= 0 || sampleRate_ <= 0.0)
        return;

    channelCount_ = std::min(channelCount, maximumChannels_);
    for (int channel = 0; channel < channelCount_; ++channel)
        channels_[static_cast<std::size_t>(channel)].weight =
            channelWeight(channel, channelCount_);

    processTruePeak(channels, channelCount_, sampleCount);

    for (int sample = 0; sample < sampleCount; ++sample) {
        double weightedPower = 0.0;
        for (int channel = 0; channel < channelCount_; ++channel) {
            const auto input = static_cast<double>(channels[channel][sample]);
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
            }
        }

        if (framesInHop_ >= hopFrames_)
            emitHop();
    }

    reading_.samplePeakLinear = samplePeak_;
    reading_.truePeakLinear = truePeak_;
    reading_.truePeakValid = truePeakReady_ && totalFrames_ > 0;
}

void LoudnessMeter::finalize() noexcept
{
    if (framesInHop_ > 0) {
        // Incomplete trailing hop — include proportionally for offline completeness.
        hopEnergyRing_[hopWrite_] = hopEnergy_ / static_cast<double>(std::max<std::size_t>(1, framesInHop_));
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
    recomputeIntegrated();
    recomputeLoudnessRange();
    reading_.samplePeakLinear = samplePeak_;
    reading_.truePeakLinear = truePeak_;
    reading_.truePeakValid = truePeakReady_ && totalFrames_ > 0;
    finalized_ = true;
}

LoudnessReading LoudnessMeter::snapshot() const noexcept
{
    return reading_;
}

} // namespace mastering::analysis
