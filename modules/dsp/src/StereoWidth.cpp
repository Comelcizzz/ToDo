#include "mastering/dsp/StereoWidth.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

void StereoWidth::prepare(double sampleRate, int /*maxBlock*/) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    midSm_.prepare(sampleRate_, 20.0);
    sideSm_.prepare(sampleRate_, 20.0);
    widthSm_.prepare(sampleRate_, 20.0);
    lowWidthSm_.prepare(sampleRate_, 20.0);
    outSm_.prepare(sampleRate_, 20.0);
    bypassSm_.prepare(sampleRate_, 15.0);
    guardSm_.prepare(sampleRate_, 50.0);
    updateCrossoverCoeff();
    reset();
}

void StereoWidth::reset() noexcept
{
    midSm_.reset(dbToGainSafe(state_.midGainDb));
    sideSm_.reset(dbToGainSafe(state_.sideGainDb));
    widthSm_.reset(std::clamp(state_.width, 0.0, 1.0));
    lowWidthSm_.reset(state_.lowBandMonoEnabled ? 0.0 : std::clamp(state_.width, 0.0, 1.0));
    outSm_.reset(dbToGainSafe(state_.outputTrimDb));
    bypassSm_.reset(state_.bypass ? 0.0 : 1.0);
    guardSm_.reset(1.0);
    sideLpState_ = 0.0;
    corrProduct_ = 0.0;
    corrLeftSq_ = 0.0;
    corrRightSq_ = 0.0;
    corrSamples_ = 0;
    meters_ = {};
}

void StereoWidth::updateCrossoverCoeff() noexcept
{
    const double hz = std::clamp(state_.lowBandMonoHz, 20.0, 2'000.0);
    // One-pole LPF: y += (1-a)*(x-y) with a = exp(-2πfc/fs). Complementary high = x-y.
    sideLpCoeff_ = std::exp(-2.0 * 3.14159265358979323846 * hz / sampleRate_);
}

void StereoWidth::setState(const StereoWidthState& state) noexcept
{
    state_ = state;
    state_.minCorrelation = std::clamp(state_.minCorrelation, -1.0, 1.0);
    state_.width = std::clamp(state_.width, 0.0, 1.0);
    midSm_.setTarget(dbToGainSafe(state_.midGainDb));
    sideSm_.setTarget(dbToGainSafe(state_.sideGainDb));
    widthSm_.setTarget(state_.width);
    lowWidthSm_.setTarget(state_.lowBandMonoEnabled ? 0.0 : state_.width);
    outSm_.setTarget(dbToGainSafe(state_.outputTrimDb));
    bypassSm_.setTarget(state_.bypass ? 0.0 : 1.0);
    updateCrossoverCoeff();
}

void StereoWidth::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;

    if (channelCount < 2) {
        meters_.correlation = 1.0;
        meters_.midGainDb = state_.midGainDb;
        meters_.sideGainDb = state_.sideGainDb;
        return;
    }

    const double a = sideLpCoeff_;
    const double oneMinusA = 1.0 - a;

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

        // Complementary one-pole split on Side only.
        sideLpState_ = a * sideLpState_ + oneMinusA * side;
        const double sideLow = sideLpState_;
        const double sideHigh = side - sideLow; // complementary residual

        const double midG = midSm_.next();
        const double sideG = sideSm_.next();
        const double width = widthSm_.next();
        const double lowWidth = lowWidthSm_.next();
        const double highWidth = width * sideG * guardGain;
        const double outG = outSm_.next();
        const double active = bypassSm_.next();

        mid *= midG;
        side = sideLow * lowWidth + sideHigh * highWidth;

        double left = (mid + side) * outG;
        double right = (mid - side) * outG;

        channels[0][i] = static_cast<float>(double(inL) * (1.0 - active) + left * active);
        channels[1][i] = static_cast<float>(double(inR) * (1.0 - active) + right * active);

        meters_.correlation = correlation;
        meters_.midGainDb = gainToDbSafe(midG);
        meters_.sideGainDb = gainToDbSafe(sideG * width * guardGain);
    }
}

} // namespace mastering::dsp
