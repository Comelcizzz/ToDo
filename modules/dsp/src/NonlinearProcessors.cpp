#include "mastering/dsp/NonlinearProcessors.h"

#include <algorithm>

namespace mastering::dsp {
namespace {

void ensureDelay(
    std::array<std::vector<float>, 2>& lines,
    std::array<std::size_t, 2>& writes,
    int& delayLength,
    int channels,
    int latency)
{
    delayLength = std::max(0, latency);
    for (int ch = 0; ch < channels; ++ch) {
        lines[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(std::max(1, delayLength)), 0.0f);
        writes[static_cast<std::size_t>(ch)] = 0;
    }
}

float readDelay(
    std::vector<float>& line,
    std::size_t& write,
    int delayLength,
    float input) noexcept
{
    if (delayLength <= 0)
        return input;
    // Size-D circular buffer: read oldest then overwrite → exact D-sample delay.
    const auto len = static_cast<std::size_t>(delayLength);
    const auto out = line[write];
    line[write] = input;
    write = (write + 1) % len;
    return out;
}

} // namespace

void SaturationProcessor::prepare(double sampleRate, int maxBlock, int channels, int factor) noexcept
{
    channels_ = std::clamp(channels, 1, 2);
    settings_.oversamplingFactor = factor;
    oversampler_.prepare(sampleRate, maxBlock, channels_, factor);
    driveSm_.prepare(sampleRate, 20.0);
    mixSm_.prepare(sampleRate, 20.0);
    outSm_.prepare(sampleRate, 20.0);
    bypassSm_.prepare(sampleRate, 15.0);
    for (int ch = 0; ch < 2; ++ch)
        dryScratch_[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(std::max(1, maxBlock)), 0.0f);
    ensureDelay(delayLine_, delayWrite_, delayLength_, channels_, oversampler_.latencySamplesBaseRate());
    reset();
}

void SaturationProcessor::reset() noexcept
{
    oversampler_.reset();
    driveSm_.reset(std::max(1.0, settings_.drive));
    mixSm_.reset(std::clamp(settings_.mix, 0.0, 1.0));
    outSm_.reset(dbToGainSafe(settings_.outputTrimDb));
    bypassSm_.reset(settings_.bypass ? 0.0 : 1.0);
    for (auto& d : delayLine_)
        std::fill(d.begin(), d.end(), 0.0f);
    delayWrite_.fill(0);
}

void SaturationProcessor::setSettings(const SaturationSettings& settings) noexcept
{
    // Realtime-safe: continuous params only. OS factor changes require prepare().
    settings_.bypass = settings.bypass;
    settings_.drive = std::max(1.0, settings.drive);
    settings_.mix = std::clamp(settings.mix, 0.0, 1.0);
    settings_.outputTrimDb = settings.outputTrimDb;
    settings_.mode = settings.mode;
    settings_.autoGainStatic = settings.autoGainStatic;
    int factor = settings.oversamplingFactor;
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = oversampler_.factor();
    settings_.oversamplingFactor = factor; // may differ from prepared until prepare()
    driveSm_.setTarget(settings_.drive);
    mixSm_.setTarget(settings_.mix);
    outSm_.setTarget(dbToGainSafe(settings_.outputTrimDb));
    bypassSm_.setTarget(settings_.bypass ? 0.0 : 1.0);
}

int SaturationProcessor::latencySamples() const noexcept
{
    return oversampler_.latencySamplesBaseRate();
}

void SaturationProcessor::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);
    const int maxN = std::max(1, oversampler_.maximumBlockSize());
    for (int offset = 0; offset < sampleCount;) {
        const int n = std::min(maxN, sampleCount - offset);
        float* slice[2] {};
        for (int ch = 0; ch < channelCount; ++ch)
            slice[ch] = channels[ch] + offset;
        // dry scratch
        for (int ch = 0; ch < channelCount; ++ch)
            for (int i = 0; i < n; ++i)
                dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = slice[ch][i];

        auto nonlinear = [&](float* const* os, int chCount, int osSamples, double) {
            for (int i = 0; i < osSamples; ++i) {
                const auto drive = std::max(1.0, driveSm_.next());
                const auto norm = std::tanh(drive);
                const auto invNorm = norm > 1.0e-12 ? 1.0 / norm : 1.0;
                for (int ch = 0; ch < chCount; ++ch) {
                    auto x = static_cast<double>(os[ch][i]);
                    if (!std::isfinite(x))
                        x = 0.0;
                    auto y = std::tanh(x * drive);
                    if (settings_.autoGainStatic)
                        y *= invNorm;
                    os[ch][i] = static_cast<float>(y);
                }
            }
        };

        if (oversampler_.factor() <= 1)
            nonlinear(slice, channelCount, n, 1.0);
        else
            oversampler_.process(slice, channelCount, n, nonlinear);

        for (int i = 0; i < n; ++i) {
            const auto mix = mixSm_.next();
            const auto outG = outSm_.next();
            const auto wetAmt = bypassSm_.next();
            for (int ch = 0; ch < channelCount; ++ch) {
                const auto dryIn = dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
                const auto delayedDry = readDelay(
                    delayLine_[static_cast<std::size_t>(ch)],
                    delayWrite_[static_cast<std::size_t>(ch)],
                    delayLength_,
                    dryIn);
                auto wet = static_cast<double>(slice[ch][i]) * outG;
                auto mixed = delayedDry + mix * (wet - delayedDry);
                auto out = delayedDry + wetAmt * (mixed - delayedDry);
                if (!std::isfinite(out))
                    out = 0.0;
                slice[ch][i] = static_cast<float>(out);
            }
        }
        offset += n;
    }
}

void SaturationProcessor::finalize(float* const*, int, int) noexcept {}

double SoftClipper::transfer(double x, double thresholdLin, double kneeLin) noexcept
{
    const auto T = std::max(1.0e-6, thresholdLin);
    const auto K = std::max(1.0e-6, kneeLin);
    const auto ax = std::abs(x);
    const auto lower = T - 0.5 * K;
    const auto upper = T + 0.5 * K;
    double ay = ax;
    if (ax <= lower)
        ay = ax;
    else if (ax >= upper)
        ay = T;
    else {
        const auto t = (ax - lower) / K;
        ay = lower + K * (t - t * t * 0.5);
    }
    return std::copysign(ay, x);
}

void SoftClipper::prepare(double sampleRate, int maxBlock, int channels, int factor) noexcept
{
    channels_ = std::clamp(channels, 1, 2);
    settings_.oversamplingFactor = factor;
    oversampler_.prepare(sampleRate, maxBlock, channels_, factor);
    threshSm_.prepare(sampleRate, 20.0);
    kneeSm_.prepare(sampleRate, 20.0);
    driveSm_.prepare(sampleRate, 20.0);
    mixSm_.prepare(sampleRate, 20.0);
    outSm_.prepare(sampleRate, 20.0);
    bypassSm_.prepare(sampleRate, 15.0);
    for (int ch = 0; ch < 2; ++ch)
        dryScratch_[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(std::max(1, maxBlock)), 0.0f);
    ensureDelay(delayLine_, delayWrite_, delayLength_, channels_, oversampler_.latencySamplesBaseRate());
    reset();
}

void SoftClipper::reset() noexcept
{
    oversampler_.reset();
    threshSm_.reset(dbToGainSafe(settings_.thresholdDb));
    const auto thr = dbToGainSafe(settings_.thresholdDb);
    const auto kneeLin = thr * (1.0 - dbToGainSafe(-std::abs(settings_.kneeDb)));
    kneeSm_.reset(std::max(1.0e-6, kneeLin));
    driveSm_.reset(dbToGainSafe(settings_.driveDb));
    mixSm_.reset(std::clamp(settings_.mix, 0.0, 1.0));
    outSm_.reset(dbToGainSafe(settings_.outputTrimDb));
    bypassSm_.reset(settings_.bypass ? 0.0 : 1.0);
    for (auto& d : delayLine_)
        std::fill(d.begin(), d.end(), 0.0f);
    delayWrite_.fill(0);
}

void SoftClipper::setSettings(const SoftClipSettings& settings) noexcept
{
    settings_.bypass = settings.bypass;
    settings_.thresholdDb = settings.thresholdDb;
    settings_.kneeDb = settings.kneeDb;
    settings_.driveDb = settings.driveDb;
    settings_.mix = std::clamp(settings.mix, 0.0, 1.0);
    settings_.outputTrimDb = settings.outputTrimDb;
    int factor = settings.oversamplingFactor;
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = oversampler_.factor();
    settings_.oversamplingFactor = factor;
    threshSm_.setTarget(dbToGainSafe(settings_.thresholdDb));
    const auto thr = dbToGainSafe(settings_.thresholdDb);
    const auto kneeLin = thr * (1.0 - dbToGainSafe(-std::abs(settings_.kneeDb)));
    kneeSm_.setTarget(std::max(1.0e-6, kneeLin));
    driveSm_.setTarget(dbToGainSafe(settings_.driveDb));
    mixSm_.setTarget(settings_.mix);
    outSm_.setTarget(dbToGainSafe(settings_.outputTrimDb));
    bypassSm_.setTarget(settings_.bypass ? 0.0 : 1.0);
}

int SoftClipper::latencySamples() const noexcept { return oversampler_.latencySamplesBaseRate(); }

void SoftClipper::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);
    const int maxN = std::max(1, oversampler_.maximumBlockSize());
    for (int offset = 0; offset < sampleCount;) {
        const int n = std::min(maxN, sampleCount - offset);
        float* slice[2] {};
        for (int ch = 0; ch < channelCount; ++ch)
            slice[ch] = channels[ch] + offset;
        for (int ch = 0; ch < channelCount; ++ch)
            for (int i = 0; i < n; ++i)
                dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = slice[ch][i];

        auto nonlinear = [&](float* const* os, int chCount, int osSamples, double) {
            for (int i = 0; i < osSamples; ++i) {
                const auto thr = threshSm_.next();
                const auto knee = kneeSm_.next();
                const auto drive = driveSm_.next();
                for (int ch = 0; ch < chCount; ++ch) {
                    auto x = static_cast<double>(os[ch][i]) * drive;
                    if (!std::isfinite(x))
                        x = 0.0;
                    os[ch][i] = static_cast<float>(transfer(x, thr, knee));
                }
            }
        };
        if (oversampler_.factor() <= 1)
            nonlinear(slice, channelCount, n, 1.0);
        else
            oversampler_.process(slice, channelCount, n, nonlinear);

        for (int i = 0; i < n; ++i) {
            const auto mix = mixSm_.next();
            const auto outG = outSm_.next();
            const auto wetAmt = bypassSm_.next();
            for (int ch = 0; ch < channelCount; ++ch) {
                const auto dryIn = dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
                const auto delayedDry = readDelay(
                    delayLine_[static_cast<std::size_t>(ch)],
                    delayWrite_[static_cast<std::size_t>(ch)],
                    delayLength_,
                    dryIn);
                auto wet = static_cast<double>(slice[ch][i]) * outG;
                auto mixed = delayedDry + mix * (wet - delayedDry);
                auto out = delayedDry + wetAmt * (mixed - delayedDry);
                if (!std::isfinite(out))
                    out = 0.0;
                slice[ch][i] = static_cast<float>(out);
            }
        }
        offset += n;
    }
}

void HardClipper::prepare(double sampleRate, int maxBlock, int channels, int factor) noexcept
{
    channels_ = std::clamp(channels, 1, 2);
    settings_.oversamplingFactor = factor;
    oversampler_.prepare(sampleRate, maxBlock, channels_, factor);
    ceilSm_.prepare(sampleRate, 20.0);
    driveSm_.prepare(sampleRate, 20.0);
    outSm_.prepare(sampleRate, 20.0);
    bypassSm_.prepare(sampleRate, 15.0);
    for (int ch = 0; ch < 2; ++ch)
        dryScratch_[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(std::max(1, maxBlock)), 0.0f);
    ensureDelay(delayLine_, delayWrite_, delayLength_, channels_, oversampler_.latencySamplesBaseRate());
    reset();
}

void HardClipper::reset() noexcept
{
    oversampler_.reset();
    ceilSm_.reset(dbToGainSafe(settings_.ceilingDb));
    driveSm_.reset(dbToGainSafe(settings_.driveDb));
    outSm_.reset(dbToGainSafe(settings_.outputTrimDb));
    bypassSm_.reset(settings_.bypass ? 0.0 : 1.0);
    for (auto& d : delayLine_)
        std::fill(d.begin(), d.end(), 0.0f);
    delayWrite_.fill(0);
}

void HardClipper::setSettings(const HardClipSettings& settings) noexcept
{
    settings_.bypass = settings.bypass;
    settings_.ceilingDb = settings.ceilingDb;
    settings_.driveDb = settings.driveDb;
    settings_.outputTrimDb = settings.outputTrimDb;
    int factor = settings.oversamplingFactor;
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = oversampler_.factor();
    settings_.oversamplingFactor = factor;
    ceilSm_.setTarget(dbToGainSafe(settings_.ceilingDb));
    driveSm_.setTarget(dbToGainSafe(settings_.driveDb));
    outSm_.setTarget(dbToGainSafe(settings_.outputTrimDb));
    bypassSm_.setTarget(settings_.bypass ? 0.0 : 1.0);
}

int HardClipper::latencySamples() const noexcept { return oversampler_.latencySamplesBaseRate(); }

void HardClipper::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);
    const int maxN = std::max(1, oversampler_.maximumBlockSize());
    for (int offset = 0; offset < sampleCount;) {
        const int n = std::min(maxN, sampleCount - offset);
        float* slice[2] {};
        for (int ch = 0; ch < channelCount; ++ch)
            slice[ch] = channels[ch] + offset;
        for (int ch = 0; ch < channelCount; ++ch)
            for (int i = 0; i < n; ++i)
                dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = slice[ch][i];

        auto nonlinear = [&](float* const* os, int chCount, int osSamples, double) {
            for (int i = 0; i < osSamples; ++i) {
                const auto ceil = std::max(1.0e-6, ceilSm_.next());
                const auto drive = driveSm_.next();
                for (int ch = 0; ch < chCount; ++ch) {
                    auto x = static_cast<double>(os[ch][i]) * drive;
                    if (!std::isfinite(x))
                        x = 0.0;
                    x = std::clamp(x, -ceil, ceil);
                    os[ch][i] = static_cast<float>(x);
                }
            }
        };
        if (oversampler_.factor() <= 1)
            nonlinear(slice, channelCount, n, 1.0);
        else
            oversampler_.process(slice, channelCount, n, nonlinear);

        for (int i = 0; i < n; ++i) {
            const auto outG = outSm_.next();
            const auto wetAmt = bypassSm_.next();
            for (int ch = 0; ch < channelCount; ++ch) {
                const auto dryIn = dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
                const auto delayedDry = readDelay(
                    delayLine_[static_cast<std::size_t>(ch)],
                    delayWrite_[static_cast<std::size_t>(ch)],
                    delayLength_,
                    dryIn);
                auto wet = static_cast<double>(slice[ch][i]) * outG;
                auto out = delayedDry + wetAmt * (wet - delayedDry);
                if (!std::isfinite(out))
                    out = 0.0;
                slice[ch][i] = static_cast<float>(out);
            }
        }
        offset += n;
    }
}

} // namespace mastering::dsp
