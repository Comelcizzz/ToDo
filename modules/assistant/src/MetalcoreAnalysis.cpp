#include "mastering/assistant/MetalcoreAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

namespace mastering::assistant {
namespace {

double rmsDb(const std::vector<float>& x)
{
    if (x.empty())
        return -120.0;
    double a = 0.0;
    for (float v : x)
        a += double(v) * double(v);
    return 20.0 * std::log10(std::max(1.0e-12, std::sqrt(a / double(x.size()))));
}

int countTransients(const std::vector<float>& mono, double sampleRate)
{
    int count = 0;
    double env = 0.0;
    const double attack = std::exp(-1.0 / (sampleRate * 0.003));
    const double release = std::exp(-1.0 / (sampleRate * 0.050));
    double prev = 0.0;
    for (float s : mono) {
        const double x = std::abs(double(s));
        env = x > env ? attack * env + (1.0 - attack) * x : release * env + (1.0 - release) * x;
        if (env > prev * 1.8 && env > 0.05)
            ++count;
        prev = env;
    }
    return count;
}

} // namespace

std::vector<float> mixToMono(const std::vector<std::vector<float>>& channels)
{
    if (channels.empty())
        return {};
    const auto n = channels.front().size();
    std::vector<float> mono(n, 0.0f);
    for (const auto& ch : channels) {
        for (std::size_t i = 0; i < n && i < ch.size(); ++i)
            mono[i] += ch[i] / float(channels.size());
    }
    return mono;
}

double MetalcoreAnalysis::toneMagnitudeDb(
    const std::vector<float>& mono,
    double sampleRate,
    double frequencyHz)
{
    if (mono.empty() || frequencyHz <= 0.0 || sampleRate <= 0.0)
        return -120.0;
    // Goertzel
    const double w = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    const auto n = std::min(mono.size(), static_cast<std::size_t>(sampleRate * 8.0));
    for (std::size_t i = 0; i < n; ++i) {
        s0 = double(mono[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 10.0 * std::log10(std::max(1.0e-24, power / double(n)));
}

std::vector<SpectralPeak> MetalcoreAnalysis::findPeaksInBand(
    const std::vector<float>& mono,
    double sampleRate,
    double lowHz,
    double highHz,
    int candidates)
{
    std::vector<SpectralPeak> peaks;
    if (mono.empty() || highHz <= lowHz)
        return peaks;

    const double step = std::max(1.0, (highHz - lowHz) / 64.0);
    std::vector<std::pair<double, double>> curve;
    for (double f = lowHz; f <= highHz; f += step)
        curve.push_back({f, toneMagnitudeDb(mono, sampleRate, f)});

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
    if (static_cast<int>(peaks.size()) > candidates)
        peaks.resize(static_cast<std::size_t>(candidates));
    return peaks;
}

double MetalcoreAnalysis::evidenceScore(
    int eventCount,
    double stability,
    double snrProxyDb,
    double sectionConsistency) noexcept
{
    const double events = std::clamp(eventCount / 16.0, 0.0, 1.0);
    const double snr = std::clamp((snrProxyDb + 10.0) / 40.0, 0.0, 1.0);
    const double score = 0.35 * std::clamp(stability, 0.0, 1.0)
        + 0.30 * events
        + 0.20 * snr
        + 0.15 * std::clamp(sectionConsistency, 0.0, 1.0);
    return std::clamp(score, 0.0, 1.0);
}

LowFrequencyProfile MetalcoreAnalysis::analyzeKickLow(
    const std::vector<float>& mono,
    double sampleRate)
{
    LowFrequencyProfile profile;
    auto peaks = findPeaksInBand(mono, sampleRate, 35.0, 120.0, 5);
    profile.fundamentalCandidates = peaks;
    if (!peaks.empty()) {
        profile.dominantLowHz = peaks.front().frequencyHz;
        profile.bodyHz = peaks.front().frequencyHz;
    }
    auto clickPeaks = findPeaksInBand(mono, sampleRate, 1'500.0, 6'000.0, 3);
    if (!clickPeaks.empty())
        profile.clickHz = clickPeaks.front().frequencyHz;

    profile.eventCount = countTransients(mono, sampleRate);
    // Sustain: fraction of energy after first 40 ms windows of peak events (coarse).
    const auto n = mono.size();
    if (n > 0) {
        const auto early = std::min(n, static_cast<std::size_t>(sampleRate * 0.04));
        double eEarly = 0.0;
        double eLate = 0.0;
        for (std::size_t i = 0; i < early; ++i)
            eEarly += double(mono[i]) * mono[i];
        for (std::size_t i = early; i < n; ++i)
            eLate += double(mono[i]) * mono[i];
        profile.sustainSeconds = eLate > eEarly * 0.25
            ? std::min(0.25, (n - early) / sampleRate)
            : 0.04;
        profile.lowDecaySeconds = profile.sustainSeconds;
    }
    profile.stabilityScore = peaks.empty()
        ? 0.1
        : std::clamp(peaks.front().prominenceDb / 12.0, 0.0, 1.0);
    std::ostringstream oss;
    oss << "kickPeaks=";
    for (const auto& p : peaks)
        oss << p.frequencyHz << "Hz/" << p.prominenceDb << "dB;";
    oss << "events=" << profile.eventCount << ";click=" << profile.clickHz;
    profile.evidence = oss.str();
    return profile;
}

LowFrequencyProfile MetalcoreAnalysis::analyzeBassLow(
    const std::vector<float>& mono,
    double sampleRate)
{
    LowFrequencyProfile profile;
    auto peaks = findPeaksInBand(mono, sampleRate, 30.0, 180.0, 6);
    profile.fundamentalCandidates = peaks;
    if (!peaks.empty()) {
        // Prefer lowest stable strong peak for bass ownership of sub.
        auto lowest = peaks.front();
        for (const auto& p : peaks) {
            if (p.frequencyHz < lowest.frequencyHz && p.prominenceDb > lowest.prominenceDb - 3.0)
                lowest = p;
        }
        profile.dominantLowHz = lowest.frequencyHz;
        profile.bodyHz = peaks.front().frequencyHz;
    }
    auto artic = findPeaksInBand(mono, sampleRate, 400.0, 1'500.0, 2);
    if (!artic.empty())
        profile.clickHz = artic.front().frequencyHz; // mid articulation marker
    profile.eventCount = std::max(1, countTransients(mono, sampleRate) / 4);
    profile.stabilityScore = peaks.size() >= 2
        ? 0.55
        : (peaks.empty() ? 0.15 : 0.4);
    if (!peaks.empty() && peaks.front().prominenceDb > 6.0)
        profile.stabilityScore = std::min(1.0, profile.stabilityScore + 0.25);
    profile.sustainSeconds = mono.empty() ? 0.0 : mono.size() / sampleRate;
    std::ostringstream oss;
    oss << "bassPeaks=";
    for (const auto& p : peaks)
        oss << p.frequencyHz << "Hz/" << p.prominenceDb << "dB;";
    profile.evidence = oss.str();
    return profile;
}

GuitarChannelProfile MetalcoreAnalysis::analyzeGuitar(
    const std::vector<float>& mono,
    double sampleRate,
    const analysis::AudioMetrics& metrics)
{
    GuitarChannelProfile g;
    g.longTermRmsDb = metrics.rmsDbfs;
    g.lowMidBuildDb = metrics.spectrum.lowMidDb;
    g.presenceDb = metrics.spectrum.presenceDb;
    g.transientDensityHz = metrics.transientDensityHz;
    g.articulationDb = metrics.spectrum.midDb;
    g.fizzEnergyDb = metrics.spectrum.airDb;
    g.spectralTiltDbPerOct = (metrics.spectrum.airDb - metrics.spectrum.bassDb) / 6.0;

    auto mudPeaks = findPeaksInBand(mono, sampleRate, 180.0, 450.0, 3);
    auto harshPeaks = findPeaksInBand(mono, sampleRate, 2'000.0, 6'000.0, 4);
    auto fizzPeaks = findPeaksInBand(mono, sampleRate, 7'000.0, 14'000.0, 3);

    const double neighbor = 0.5 * (metrics.spectrum.bassDb + metrics.spectrum.midDb);
    g.mudLikely = metrics.spectrum.lowMidDb > neighbor + 3.0
        && metrics.spectrum.lowMidDb > -22.0;
    if (!mudPeaks.empty() && mudPeaks.front().prominenceDb > 2.0)
        g.mudLikely = true;

    if (!harshPeaks.empty() && harshPeaks.front().prominenceDb > 3.0) {
        g.harshLikely = true;
        g.harshPeakHz = harshPeaks.front().frequencyHz;
        g.harshPeakDb = harshPeaks.front().magnitudeDb;
        g.harshQ = std::clamp(harshPeaks.front().frequencyHz / std::max(40.0, harshPeaks.front().bandwidthHz), 1.5, 8.0);
    } else if (metrics.spectrum.presenceDb > metrics.spectrum.midDb + 4.0) {
        g.harshLikely = true;
        g.harshPeakHz = 3'200.0;
        g.harshPeakDb = metrics.spectrum.presenceDb;
        g.harshQ = 1.8;
    }

    g.fizzLikely = metrics.spectrum.airDb > metrics.spectrum.presenceDb + 2.0
        && metrics.spectrum.airDb > -18.0;
    if (!fizzPeaks.empty() && fizzPeaks.front().prominenceDb > 2.5)
        g.fizzLikely = true;

    std::ostringstream oss;
    oss << "mud=" << g.mudLikely << ";harshHz=" << g.harshPeakHz
        << ";fizz=" << g.fizzLikely << ";tilt=" << g.spectralTiltDbPerOct;
    g.evidence = oss.str();
    return g;
}

VocalProfile MetalcoreAnalysis::analyzeVocal(
    const std::vector<float>& mono,
    double sampleRate,
    const analysis::AudioMetrics& metrics,
    project::TrackRole role)
{
    VocalProfile v;
    v.crestDb = metrics.crestFactorDb;
    v.rideTargetDb = role == project::TrackRole::screamVocal ? -16.0 : -18.0;
    v.presenceCentroidHz = 2'500.0;
    auto presencePeaks = findPeaksInBand(mono, sampleRate, 1'500.0, 4'500.0, 3);
    if (!presencePeaks.empty())
        v.presenceCentroidHz = presencePeaks.front().frequencyHz;

    auto essPeaks = findPeaksInBand(mono, sampleRate, 5'000.0, 10'000.0, 3);
    if (!essPeaks.empty()) {
        v.sibilanceHz = essPeaks.front().frequencyHz;
        v.sibilanceEnergyDb = essPeaks.front().magnitudeDb;
        v.needsDeEss = essPeaks.front().prominenceDb > 3.0
            || (role == project::TrackRole::cleanVocal && metrics.spectrum.airDb > -16.0);
    }

    auto resPeaks = findPeaksInBand(mono, sampleRate, 800.0, 3'500.0, 4);
    if (!resPeaks.empty() && resPeaks.front().prominenceDb > 4.0) {
        v.resonanceHz = resPeaks.front().frequencyHz;
        v.resonanceDb = resPeaks.front().magnitudeDb;
        v.needsResonance = true;
    }

    // Activity ratio via envelope threshold.
    int active = 0;
    for (float s : mono) {
        if (20.0 * std::log10(std::max(1.0e-9, std::abs(double(s)))) > -40.0)
            ++active;
    }
    v.activityRatio = mono.empty() ? 0.0 : double(active) / double(mono.size());
    v.needsRide = v.crestDb > 14.0 || (v.activityRatio > 0.15 && metrics.rmsDbfs < -24.0);
    v.needsPeakComp = role == project::TrackRole::screamVocal
        ? (v.crestDb > 12.0)
        : (v.crestDb > 16.0);

    std::ostringstream oss;
    oss << "role=" << project::roleToString(role)
        << ";activity=" << v.activityRatio
        << ";essHz=" << v.sibilanceHz
        << ";resHz=" << v.resonanceHz
        << ";ride=" << v.needsRide;
    v.evidence = oss.str();
    return v;
}

SnareProfile MetalcoreAnalysis::analyzeSnare(
    const std::vector<float>& mono,
    double sampleRate,
    const analysis::AudioMetrics& metrics)
{
    SnareProfile s;
    auto body = findPeaksInBand(mono, sampleRate, 150.0, 280.0, 2);
    auto crack = findPeaksInBand(mono, sampleRate, 1'800.0, 4'500.0, 3);
    auto ring = findPeaksInBand(mono, sampleRate, 400.0, 900.0, 2);
    if (!body.empty())
        s.bodyHz = body.front().frequencyHz;
    if (!crack.empty()) {
        s.crackHz = crack.front().frequencyHz;
        s.crackEnergyDb = crack.front().magnitudeDb;
    }
    if (!ring.empty())
        s.ringHz = ring.front().frequencyHz;
    s.eventRateHz = metrics.transientDensityHz > 0.0
        ? metrics.transientDensityHz
        : countTransients(mono, sampleRate) / std::max(0.1, mono.size() / sampleRate);
    std::ostringstream oss;
    oss << "body=" << s.bodyHz << ";crack=" << s.crackHz << ";ring=" << s.ringHz
        << ";rate=" << s.eventRateHz;
    s.evidence = oss.str();
    return s;
}

SubOwnershipDecision MetalcoreAnalysis::decideSubOwnership(
    const LowFrequencyProfile& kick,
    const LowFrequencyProfile& bass,
    const analysis::AudioMetrics& kickMetrics,
    const analysis::AudioMetrics& bassMetrics)
{
    SubOwnershipDecision d;
    d.kickDominantHz = kick.dominantLowHz;
    d.bassDominantHz = bass.dominantLowHz;
    d.overlapHz = 0.5 * (kick.dominantLowHz + bass.dominantLowHz);
    d.overlapWidthHz = std::abs(kick.dominantLowHz - bass.dominantLowHz);

    const bool kickStable = kick.stabilityScore >= 0.35 && kick.dominantLowHz > 0.0;
    const bool bassStable = bass.stabilityScore >= 0.35 && bass.dominantLowHz > 0.0;
    const bool kickHasSub = kickStable && kick.dominantLowHz < 70.0
        && kickMetrics.spectrum.subDb > -30.0;
    const bool bassHasLower = bassStable && bass.dominantLowHz + 5.0 < kick.dominantLowHz
        && bassMetrics.spectrum.subDb > -28.0;

    std::ostringstream trace;
    trace << "1.kickStable=" << kickStable << "@" << kick.dominantLowHz
          << ";2.bassStable=" << bassStable << "@" << bass.dominantLowHz
          << ";3.kickHasSub=" << kickHasSub
          << ";4.bassHasLower=" << bassHasLower;

    if (!kickStable && !bassStable) {
        d.ownership = SubOwnership::uncertain;
        d.evidenceScore = 0.2;
        trace << ";result=uncertain";
    } else if (kickHasSub && !bassHasLower) {
        d.ownership = SubOwnership::kickOwnsSub;
        d.evidenceScore = evidenceScore(kick.eventCount, kick.stabilityScore, 12.0);
        trace << ";result=kickOwns";
    } else if (bassHasLower && !kickHasSub) {
        d.ownership = SubOwnership::bassOwnsSub;
        d.evidenceScore = evidenceScore(bass.eventCount, bass.stabilityScore, 10.0);
        trace << ";result=bassOwns";
    } else if (kickStable && bassStable && d.overlapWidthHz < 18.0) {
        d.ownership = SubOwnership::sharedButSeparated;
        d.evidenceScore = evidenceScore(
            kick.eventCount + bass.eventCount,
            0.5 * (kick.stabilityScore + bass.stabilityScore),
            8.0);
        d.overlapHz = 0.5 * (kick.dominantLowHz + bass.dominantLowHz);
        trace << ";result=sharedSeparated";
    } else if (kickStable && bassStable) {
        d.ownership = SubOwnership::sharedButSeparated;
        d.evidenceScore = 0.55;
        trace << ";result=sharedSeparated-wide";
    } else {
        d.ownership = SubOwnership::uncertain;
        d.evidenceScore = 0.3;
        trace << ";result=uncertain-fallback";
    }
    d.decisionTrace = trace.str();
    return d;
}

ReferenceProfile MetalcoreAnalysis::buildReferenceProfile(
    const analysis::AudioMetrics& metrics,
    const std::string& role,
    double vocalToBedDb,
    double kickToBassDb)
{
    ReferenceProfile p;
    p.role = role;
    p.metrics = metrics;
    p.subBassRatioDb = metrics.spectrum.subDb - metrics.spectrum.bassDb;
    p.lowMidDensityDb = metrics.spectrum.lowMidDb;
    p.presenceDb = metrics.spectrum.presenceDb;
    p.highEnergyDb = metrics.spectrum.airDb;
    p.crestDb = metrics.crestFactorDb;
    p.transientDensityHz = metrics.transientDensityHz;
    p.stereoCorrelation = metrics.stereoCorrelation;
    p.vocalToBedDb = vocalToBedDb;
    p.kickToBassDb = kickToBassDb;
    p.notes = "Loudness-matched comparison only; never blind-copy EQ.";
    return p;
}

} // namespace mastering::assistant
