#include "mastering/analysis/SpectralAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mastering::analysis {

double SpectralAnalysis::toneMagnitudeDb(
    const float* mono,
    std::size_t frames,
    double sampleRate,
    double frequencyHz) noexcept
{
    if (mono == nullptr || frames == 0 || frequencyHz <= 0.0 || sampleRate <= 0.0)
        return -120.0;

    const double w = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        s0 = double(mono[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 10.0 * std::log10(std::max(1.0e-24, power / double(frames)));
}

double SpectralAnalysis::toneMagnitudeDb(
    const std::vector<float>& mono,
    double sampleRate,
    double frequencyHz) noexcept
{
    return toneMagnitudeDb(mono.data(), mono.size(), sampleRate, frequencyHz);
}

std::vector<SpectralPeak> SpectralAnalysis::findPeaksInBand(
    const float* mono,
    std::size_t frames,
    double sampleRate,
    double lowHz,
    double highHz,
    int candidates)
{
    std::vector<SpectralPeak> peaks;
    if (mono == nullptr || frames == 0 || highHz <= lowHz || sampleRate <= 0.0)
        return peaks;

    const double step = std::max(1.0, (highHz - lowHz) / 64.0);
    std::vector<std::pair<double, double>> curve;
    curve.reserve(static_cast<std::size_t>((highHz - lowHz) / step) + 2);
    for (double f = lowHz; f <= highHz; f += step)
        curve.push_back({f, toneMagnitudeDb(mono, frames, sampleRate, f)});

    for (std::size_t i = 1; i + 1 < curve.size(); ++i) {
        if (curve[i].second > curve[i - 1].second && curve[i].second > curve[i + 1].second) {
            SpectralPeak p;
            p.frequencyHz = curve[i].first;
            p.magnitudeDb = curve[i].second;
            p.prominenceDb = curve[i].second
                - 0.5 * (curve[i - 1].second + curve[i + 1].second);
            p.bandwidthHz = step * 2.0;
            peaks.push_back(p);
        }
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b) {
        return a.prominenceDb > b.prominenceDb;
    });
    if (candidates > 0 && static_cast<int>(peaks.size()) > candidates)
        peaks.resize(static_cast<std::size_t>(candidates));
    return peaks;
}

std::vector<SpectralPeak> SpectralAnalysis::findPeaksInBand(
    const std::vector<float>& mono,
    double sampleRate,
    double lowHz,
    double highHz,
    int candidates)
{
    return findPeaksInBand(mono.data(), mono.size(), sampleRate, lowHz, highHz, candidates);
}

std::vector<double> SpectralAnalysis::countOnsets(
    const float* mono,
    std::size_t frames,
    double sampleRate,
    double minSpacingSeconds,
    double riseRatio,
    double floorLinear)
{
    std::vector<double> times;
    if (mono == nullptr || frames == 0 || sampleRate <= 0.0)
        return times;

    // Fast attack / slower release envelope. Onset = sample rising above the
    // current envelope by riseRatio (compare input to env *before* update),
    // which works for impulsive kicks; consecutive-env ratios with a slow
    // attack never reach typical riseRatio thresholds.
    double env = 0.0;
    const double attack = std::exp(-1.0 / (sampleRate * 0.0008));
    const double release = std::exp(-1.0 / (sampleRate * 0.050));
    double lastOnset = -1.0e9;

    for (std::size_t i = 0; i < frames; ++i) {
        const double x = std::abs(double(mono[i]));
        const double t = double(i) / sampleRate;
        if (x > std::max(env * riseRatio, floorLinear)
            && (t - lastOnset) >= minSpacingSeconds) {
            times.push_back(t);
            lastOnset = t;
        }
        env = x > env
            ? attack * env + (1.0 - attack) * x
            : release * env + (1.0 - release) * x;
    }
    return times;
}

std::vector<double> SpectralAnalysis::countOnsets(
    const std::vector<float>& mono,
    double sampleRate,
    double minSpacingSeconds,
    double riseRatio,
    double floorLinear)
{
    return countOnsets(
        mono.data(),
        mono.size(),
        sampleRate,
        minSpacingSeconds,
        riseRatio,
        floorLinear);
}

double SpectralAnalysis::median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    if (n % 2 == 1)
        return values[n / 2];
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

} // namespace mastering::analysis
