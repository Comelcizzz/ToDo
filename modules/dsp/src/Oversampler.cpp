#include "mastering/dsp/Oversampler.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

void Oversampler::designKernels() noexcept
{
    if (factor_ <= 1) {
        latencySamples_ = 0;
        fractionalLatencyResidual_ = 0.0;
        return;
    }

    // Prototype lowpass for interpolation/decimation: cutoff ≈ 0.45 * Nyquist_base
    // in the OS domain (radians/OS-sample).
    const auto cutoff = 0.45 * kPi / static_cast<double>(factor_);
    const int prototypeLength = factor_ * kTapsPerPhase;
    const int half = prototypeLength / 2;

    // Design full prototype then split into polyphase branches.
    std::array<double, kMaxFactor * kTapsPerPhase> prototype {};
    double sum = 0.0;
    for (int i = 0; i < prototypeLength; ++i) {
        const auto index = i - half;
        double value = 0.0;
        if (index == 0)
            value = cutoff / kPi;
        else {
            const auto x = static_cast<double>(index);
            value = std::sin(cutoff * x) / (kPi * x);
        }
        const auto window = 0.5
            + 0.5 * std::cos(kPi * static_cast<double>(index) / static_cast<double>(half));
        prototype[static_cast<std::size_t>(i)] = value * window;
        sum += prototype[static_cast<std::size_t>(i)];
    }
    // Unity DC for the full lowpass used in decimation / for phase extraction.
    if (std::abs(sum) > 1.0e-12) {
        for (int i = 0; i < prototypeLength; ++i)
            prototype[static_cast<std::size_t>(i)] /= sum;
    }

    // Polyphase branches for interpolation: gain L overall → multiply by factor when splitting.
    for (int phase = 0; phase < factor_; ++phase) {
        double phaseSum = 0.0;
        for (int tap = 0; tap < kTapsPerPhase; ++tap) {
            const auto idx = tap * factor_ + phase;
            const auto c = prototype[static_cast<std::size_t>(idx)] * static_cast<double>(factor_);
            coeffs_[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)] = c;
            phaseSum += c;
        }
        // Keep phase DC ≈ 1 for stable interpolation of constants.
        if (std::abs(phaseSum) > 1.0e-12) {
            for (auto& c : coeffs_[static_cast<std::size_t>(phase)])
                c /= phaseSum;
        }
    }

    // Store prototype (unity DC) into coeffs_ scratch via downCoeffs_ — use coeffs_ layout
    // differently: keep interpolator in coeffs_, store decimator prototype in downPrototype_.
    for (int i = 0; i < prototypeLength; ++i)
        downPrototype_[static_cast<std::size_t>(i)] = prototype[static_cast<std::size_t>(i)];

    // Round-trip group delay (base-rate samples):
    //   N = full prototype length = factor * kTapsPerPhase
    //   M = taps per phase = kTapsPerPhase
    //   L = factor
    //   upDelayBase   = (M - 1) / 2
    //   downDelayBase = (N - 1) / (2 * L)
    //   totalBase     = upDelayBase + downDelayBase
    // Host reports nearest integer; fractional residual = totalBase - reported.
    const double upBase = 0.5 * static_cast<double>(kTapsPerPhase - 1);
    const double downBase =
        0.5 * static_cast<double>(prototypeLength - 1) / static_cast<double>(factor_);
    const double totalBase = upBase + downBase;
    latencySamples_ = static_cast<int>(std::lround(totalBase));
    fractionalLatencyResidual_ = totalBase - static_cast<double>(latencySamples_);
}

void Oversampler::prepare(
    double sampleRate,
    int maximumBlockSize,
    int channelCount,
    int factor) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maximumBlockSize_ = std::max(1, maximumBlockSize);
    channelCount_ = std::clamp(channelCount, 1, kMaxChannels);

    int f = factor;
    if (f != 1 && f != 2 && f != 4 && f != 8)
        f = 4;
    factor_ = f;

    designKernels();

    const auto osFrames =
        static_cast<std::size_t>(maximumBlockSize_) * static_cast<std::size_t>(std::max(1, factor_));
    const auto proto = static_cast<std::size_t>(std::max(1, factor_ * kTapsPerPhase));
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        upBuffers_[static_cast<std::size_t>(ch)].assign(std::max<std::size_t>(osFrames, 1), 0.0f);
        upChannelPointers_[static_cast<std::size_t>(ch)] =
            upBuffers_[static_cast<std::size_t>(ch)].data();
        downOsHistory_[static_cast<std::size_t>(ch)].assign(proto, 0.0);
    }
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& h : upHistory_)
        h.fill(0.0);
    for (auto& h : downOsHistory_)
        std::fill(h.begin(), h.end(), 0.0);
    for (auto& buf : upBuffers_)
        std::fill(buf.begin(), buf.end(), 0.0f);
}

void Oversampler::processUp(
    const float* const* input,
    float* const* upsampled,
    int channelCount,
    int sampleCount) noexcept
{
    if (input == nullptr || upsampled == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channelCount_);

    if (factor_ <= 1) {
        for (int ch = 0; ch < channelCount; ++ch)
            for (int i = 0; i < sampleCount; ++i)
                upsampled[ch][i] = input[ch][i];
        return;
    }

    for (int n = 0; n < sampleCount; ++n) {
        for (int ch = 0; ch < channelCount; ++ch) {
            auto& history = upHistory_[static_cast<std::size_t>(ch)];
            for (int t = kTapsPerPhase - 1; t > 0; --t)
                history[static_cast<std::size_t>(t)] = history[static_cast<std::size_t>(t - 1)];
            history[0] = static_cast<double>(input[ch][n]);
            if (!std::isfinite(history[0]))
                history[0] = 0.0;

            for (int phase = 0; phase < factor_; ++phase) {
                double y = 0.0;
                const auto& c = coeffs_[static_cast<std::size_t>(phase)];
                for (int t = 0; t < kTapsPerPhase; ++t)
                    y += c[static_cast<std::size_t>(t)] * history[static_cast<std::size_t>(t)];
                upsampled[ch][n * factor_ + phase] = static_cast<float>(y);
            }
        }
    }
}

void Oversampler::processDown(
    const float* const* upsampled,
    float* const* output,
    int channelCount,
    int sampleCount) noexcept
{
    if (upsampled == nullptr || output == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channelCount_);

    if (factor_ <= 1) {
        for (int ch = 0; ch < channelCount; ++ch)
            for (int i = 0; i < sampleCount; ++i)
                output[ch][i] = upsampled[ch][i];
        return;
    }

    const int proto = factor_ * kTapsPerPhase;
    for (int n = 0; n < sampleCount; ++n) {
        for (int ch = 0; ch < channelCount; ++ch) {
            auto& history = downOsHistory_[static_cast<std::size_t>(ch)];
            for (int phase = 0; phase < factor_; ++phase) {
                // Shift right, push newest at [0].
                for (int i = proto - 1; i > 0; --i)
                    history[static_cast<std::size_t>(i)] = history[static_cast<std::size_t>(i - 1)];
                const auto s = static_cast<double>(upsampled[ch][n * factor_ + phase]);
                history[0] = std::isfinite(s) ? s : 0.0;
            }
            double y = 0.0;
            for (int k = 0; k < proto; ++k)
                y += downPrototype_[static_cast<std::size_t>(proto - 1 - k)]
                    * history[static_cast<std::size_t>(k)];
            output[ch][n] = static_cast<float>(y);
        }
    }
}

} // namespace mastering::dsp
