#include "mastering/dsp/StereoWidth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mastering::dsp {

void StereoWidth::prepare(double sampleRate, int /*maxBlock*/) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    midSm_.prepare(sampleRate_, 20.0);
    sideSm_.prepare(sampleRate_, 20.0);
    outSm_.prepare(sampleRate_, 20.0);
    bypassSm_.prepare(sampleRate_, 15.0);
    guardSm_.prepare(sampleRate_, 50.0);
    updateLowShelfCoeff();
    reset();
}

void StereoWidth::reset() noexcept
{
    midSm_.reset(dbToGainSafe(state_.midGainDb));
    sideSm_.reset(dbToGainSafe(state_.sideGainDb));
    outSm_.reset(dbToGainSafe(state_.outputTrimDb));
    bypassSm_.reset(state_.bypass ? 0.0 : 1.0);
    guardSm_.reset(1.0);
    sideLpState_.fill(0.0);
    corrProduct_ = 0.0;
    corrLeftSq_ = 0.0;
    corrRightSq_ = 0.0;
    corrSamples_ = 0;
    meters_ = {};
}

void StereoWidth::updateLowShelfCoeff() noexcept
{
    const double hz = std::clamp(state_.lowBandMonoHz, 20.0, 2'000.0);
    // One-pole LPF coefficient for side low extraction.
    sideLpCoeff_ = std::exp(-2.0 * 3.14159265358979323846 * hz / sampleRate_);
}

void StereoWidth::setState(const StereoWidthState& state) noexcept
{
    state_ = state;
    state_.minCorrelation = std::clamp(state_.minCorrelation, -1.0, 1.0);
    midSm_.setTarget(dbToGainSafe(state_.midGainDb));
    sideSm_.setTarget(dbToGainSafe(state_.sideGainDb));
    outSm_.setTarget(dbToGainSafe(state_.outputTrimDb));
    bypassSm_.setTarget(state_.bypass ? 0.0 : 1.0);
    updateLowShelfCoeff();
}

void StereoWidth::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;

    // Mono pass-through.
    if (channelCount < 2) {
        meters_.correlation = 1.0;
        meters_.midGainDb = state_.midGainDb;
        meters_.sideGainDb = state_.sideGainDb;
        return;
    }

    for (int i = 0; i < sampleCount; ++i) {
        const float inL = channels[0][i];
        const float inR = channels[1][i];

        corrProduct_ += double(inL) * double(inR);
        corrLeftSq_ += double(inL) * double(inL);
        corrRightSq_ += double(inR) * double(inR);
        ++corrSamples_;

        double correlation = 1.0;
        const double denom = std::sqrt(std::max(1.0e-24, corrLeftSq_ * corrRightSq_));
        if (corrSamples_ > 0)
            correlation = corrProduct_ / denom;
        correlation = std::clamp(correlation, -1.0, 1.0);

        // Correlation guard: if correlation drops below minCorrelation, reduce side.
        double guard = 1.0;
        if (correlation < state_.minCorrelation) {
            const double span = std::max(1.0e-6, state_.minCorrelation + 1.0);
            guard = std::clamp((correlation + 1.0) / span, 0.0, 1.0);
        }
        guardSm_.setTarget(guard);
        const double guardGain = guardSm_.next();

        // M/S encode.
        double mid = 0.5 * (double(inL) + double(inR));
        double side = 0.5 * (double(inL) - double(inR));

        // Low-band mono: extract low side via LPF and remove it (force side→0 below cutoff).
        sideLpState_[0] = sideLpCoeff_ * sideLpState_[0] + (1.0 - sideLpCoeff_) * side;
        const double sideLow = sideLpState_[0];
        const double sideHigh = side - sideLow;
        side = sideHigh; // low side discarded → mono bass
        (void) sideLpState_[1];

        const double midG = midSm_.next();
        const double sideG = sideSm_.next() * guardGain;
        const double outG = outSm_.next();
        const double active = bypassSm_.next();

        mid *= midG;
        side *= sideG;

        // M/S decode + output compensation.
        double left = (mid + side) * outG;
        double right = (mid - side) * outG;

        channels[0][i] = static_cast<float>(double(inL) * (1.0 - active) + left * active);
        channels[1][i] = static_cast<float>(double(inR) * (1.0 - active) + right * active);

        meters_.correlation = correlation;
        meters_.midGainDb = gainToDbSafe(midG);
        meters_.sideGainDb = gainToDbSafe(sideG);
    }
}

} // namespace mastering::dsp
