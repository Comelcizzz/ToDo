#include "mastering/dsp/ParallelCompressor.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

void ParallelCompressor::prepare(double sampleRate, int maxBlock, int channels) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maxBlock_ = std::max(1, maxBlock);
    channels_ = std::clamp(channels, 1, 2);
    for (int ch = 0; ch < 2; ++ch)
        dryScratch_[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(maxBlock_),
            0.0f);
    wetSm_.prepare(sampleRate_, 15.0);
    makeupSm_.prepare(sampleRate_, 15.0);
    bypassSm_.prepare(sampleRate_, 10.0);
    driveSm_.prepare(sampleRate_, 15.0);
    reset();
}

void ParallelCompressor::reset() noexcept
{
    envelope_ = 0.0;
    meters_ = {};
    wetSm_.reset(std::clamp(state_.wetAmount, 0.0, 1.0));
    makeupSm_.reset(dbToGainSafe(state_.makeupDb));
    bypassSm_.reset(state_.bypass ? 0.0 : 1.0);
    driveSm_.reset(std::max(1.0, state_.saturationDrive));
    for (auto& buf : dryScratch_)
        std::fill(buf.begin(), buf.end(), 0.0f);

    const double atk = std::max(0.1, state_.attackMs) * 0.001;
    const double rel = std::max(1.0, state_.releaseMs) * 0.001;
    attackCoeff_ = std::exp(-1.0 / (atk * sampleRate_));
    releaseCoeff_ = std::exp(-1.0 / (rel * sampleRate_));
}

void ParallelCompressor::setState(const ParallelCompressorState& state) noexcept
{
    state_ = state;
    state_.ratio = std::max(1.0, state_.ratio);
    state_.wetAmount = std::clamp(state_.wetAmount, 0.0, 1.0);
    state_.saturationDrive = std::max(1.0, state_.saturationDrive);
    wetSm_.setTarget(state_.wetAmount);
    makeupSm_.setTarget(dbToGainSafe(state_.makeupDb));
    bypassSm_.setTarget(state_.bypass ? 0.0 : 1.0);
    driveSm_.setTarget(state_.saturationDrive);

    const double atk = std::max(0.1, state_.attackMs) * 0.001;
    const double rel = std::max(1.0, state_.releaseMs) * 0.001;
    attackCoeff_ = std::exp(-1.0 / (atk * sampleRate_));
    releaseCoeff_ = std::exp(-1.0 / (rel * sampleRate_));
}

void ParallelCompressor::clearMeters() noexcept
{
    meters_ = {};
}

double ParallelCompressor::computeGainDb(double levelDb) const noexcept
{
    const double over = levelDb - state_.thresholdDb;
    if (over <= 0.0)
        return 0.0;
    const double compressed = over / state_.ratio;
    return compressed - over; // negative GR
}

void ParallelCompressor::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);
    sampleCount = std::min(sampleCount, maxBlock_);

    double grSum = 0.0;
    double wetSum = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        // Dry copy at 0 latency (same sample index — no delay line).
        for (int c = 0; c < channelCount; ++c) {
            const auto idx = static_cast<std::size_t>(c);
            dryScratch_[idx][static_cast<std::size_t>(i)] = channels[c][i];
        }

        // Linked stereo peak detector.
        double peak = 0.0;
        for (int c = 0; c < channelCount; ++c)
            peak = std::max(peak, std::abs(double(channels[c][i])));

        envelope_ = peak > envelope_
            ? attackCoeff_ * envelope_ + (1.0 - attackCoeff_) * peak
            : releaseCoeff_ * envelope_ + (1.0 - releaseCoeff_) * peak;

        const double levelDb = gainToDbSafe(envelope_);
        const double grDb = computeGainDb(levelDb);
        const double grLin = dbToGainSafe(grDb);
        grSum += grDb;

        const double wet = wetSm_.next();
        const double makeup = makeupSm_.next();
        const double active = bypassSm_.next();
        const double drive = driveSm_.next();
        wetSum += wet;

        // wet = compress(dry) * wetAmount → out = dry*(1-wet) + wet*makeup
        // Optional soft saturation on wet path only.
        for (int c = 0; c < channelCount; ++c) {
            const float dry = dryScratch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
            double wetSample = double(dry) * grLin;

            if (state_.saturationEnabled && drive > 1.0) {
                wetSample = std::tanh(wetSample * drive) / std::tanh(drive);
            }

            wetSample *= makeup;
            const double mixed = double(dry) * (1.0 - wet) + wetSample * wet;
            const double out = double(dry) * (1.0 - active) + mixed * active;
            channels[c][i] = static_cast<float>(out);
        }
    }

    meters_.gainReductionDb = sampleCount > 0 ? grSum / double(sampleCount) : 0.0;
    meters_.wetAmount = sampleCount > 0 ? wetSum / double(sampleCount) : state_.wetAmount;
}

} // namespace mastering::dsp
