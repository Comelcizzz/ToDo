#include "mastering/dsp/VocalRider.h"

#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

void VocalRider::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    reset();
}

void VocalRider::reset() noexcept
{
    envelope_ = 0.0;
    currentGainDb_ = 0.0;
}

float VocalRider::processEnvelopeSample(float monoSample) noexcept
{
    const double x = std::abs(double(monoSample));
    const double attack = std::exp(-1.0 / (sampleRate_ * 0.020));
    const double release = std::exp(-1.0 / (sampleRate_ * 0.200));
    envelope_ = x > envelope_
        ? attack * envelope_ + (1.0 - attack) * x
        : release * envelope_ + (1.0 - release) * x;

    const double levelDb = 20.0 * std::log10(std::max(1.0e-9, envelope_));
    double desired = 0.0;
    if (levelDb > silenceThresholdDb_) {
        desired = std::clamp(targetRmsDb_ - levelDb, -maxCutDb_, maxBoostDb_);
        // Soften transient response: scale by slow envelope agreement.
        desired *= 0.65;
    }

    const double smooth = std::exp(-1.0 / (sampleRate_ * std::max(0.020, smoothingMs_ * 0.001)));
    currentGainDb_ = smooth * currentGainDb_ + (1.0 - smooth) * desired;
    return static_cast<float>(dbToGain(currentGainDb_));
}

void VocalRider::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;
    for (int i = 0; i < sampleCount; ++i) {
        float mono = 0.0f;
        for (int c = 0; c < channelCount; ++c)
            mono += channels[c][i];
        mono /= float(channelCount);
        const float g = processEnvelopeSample(mono);
        for (int c = 0; c < channelCount; ++c)
            channels[c][i] *= g;
    }
}

} // namespace mastering::dsp
