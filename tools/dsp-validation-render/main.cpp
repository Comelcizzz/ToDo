#include "mastering/analysis/LoudnessMeter.h"
#include "mastering/dsp/ExportQc.h"
#include "mastering/dsp/MasterSafetyChain.h"
#include "mastering/dsp/NonlinearProcessors.h"
#include "mastering/dsp/Oversampler.h"
#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Audio = std::vector<std::vector<float>>;

constexpr double kDefaultSampleRate = 48'000.0;
constexpr double kCeilingOvershootToleranceDb = 0.10;
constexpr double kCeilingUndershootToleranceDb = 0.20;
constexpr double kDocumentedReconstructionGuardDb = 0.12;

struct WavRecord {
    fs::path path;
    double sampleRate {0.0};
    int channels {0};
    std::size_t frames {0};
    bool finite {true};
};

struct CeilingRow {
    std::string signal;
    double ceilingDb {0.0};
    double truePeakDb {0.0};
    double errorDb {0.0};
    double maxGrDb {0.0};
    std::uint64_t clampActivations {0};
    bool pass {false};
};

struct Spectrum {
    std::vector<double> magnitude;
    double binHz {0.0};
};

struct AliasingResult {
    std::string processor;
    int factor {1};
    std::string settings;
    double metric {0.0};
    double deltaVs1x {0.0};
    std::string result;
};

std::string fixed(double value, int precision = 3)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string scientific(double value, int precision = 6)
{
    std::ostringstream out;
    out << std::scientific << std::setprecision(precision) << value;
    return out.str();
}

std::string passFail(bool pass) { return pass ? "PASS" : "FAIL"; }

const char* qcStatusName(mastering::dsp::QcStatus status)
{
    using mastering::dsp::QcStatus;
    return status == QcStatus::pass ? "PASS" : status == QcStatus::warning ? "WARNING" : "FAIL";
}

bool allFinite(const Audio& channels)
{
    for (const auto& channel : channels)
        for (float sample : channel)
            if (!std::isfinite(sample))
                return false;
    return true;
}

void writeWav(const fs::path& path, const Audio& channels, double sampleRate)
{
    if (channels.empty() || channels.front().empty())
        return;

    fs::create_directories(path.parent_path());

    const int channelCount = static_cast<int>(channels.size());
    const int frames = static_cast<int>(channels.front().size());
    std::ofstream out(path, std::ios::binary);

    auto write32 = [&](std::uint32_t v) {
        const char bytes[4] {
            static_cast<char>(v),
            static_cast<char>(v >> 8),
            static_cast<char>(v >> 16),
            static_cast<char>(v >> 24),
        };
        out.write(bytes, 4);
    };

    auto write16 = [&](std::uint16_t v) {
        const char bytes[2] {static_cast<char>(v), static_cast<char>(v >> 8)};
        out.write(bytes, 2);
    };

    const auto dataSize = static_cast<std::uint32_t>(frames * channelCount * 4);
    out.write("RIFF", 4);
    write32(36 + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write32(16);
    write16(3); // IEEE float
    write16(static_cast<std::uint16_t>(channelCount));
    write32(static_cast<std::uint32_t>(sampleRate));
    write32(static_cast<std::uint32_t>(sampleRate * channelCount * 4));
    write16(static_cast<std::uint16_t>(channelCount * 4));
    write16(32);
    out.write("data", 4);
    write32(dataSize);

    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channelCount; ++channel) {
            const float sample =
                channels[static_cast<std::size_t>(channel)][static_cast<std::size_t>(frame)];
            out.write(reinterpret_cast<const char*>(&sample), 4);
        }
    }
}

void writeRecordedWav(
    std::vector<WavRecord>& wavs,
    const fs::path& path,
    const Audio& channels,
    double sampleRate)
{
    writeWav(path, channels, sampleRate);
    wavs.push_back(WavRecord {
        path,
        sampleRate,
        static_cast<int>(channels.size()),
        channels.empty() ? 0U : channels.front().size(),
        allFinite(channels),
    });
}

std::string fnv1a64Hex(const fs::path& path)
{
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;

    std::ifstream in(path, std::ios::binary);
    std::array<char, 8192> buffer {};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= kPrime;
        }
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

Audio makeDry(double sampleRate, double seconds)
{
    const int frames = static_cast<int>(sampleRate * seconds);
    Audio channels(2, std::vector<float>(static_cast<std::size_t>(frames), 0.0f));

    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double beat = std::fmod(t, 0.5);
        const float kick = beat < 0.08
            ? static_cast<float>(
                  0.9 * std::exp(-40.0 * beat)
                  * std::sin(2.0 * std::numbers::pi * (55.0 + 35.0 * std::exp(-35.0 * beat)) * t))
            : 0.0f;
        const float mid = static_cast<float>(0.35 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t));
        const float hi = static_cast<float>(0.2 * std::sin(2.0 * std::numbers::pi * 12'000.0 * t));

        const double snarePhase = std::fmod(t, 0.75);
        const std::uint32_t noiseWord =
            static_cast<std::uint32_t>(i) * 1'103'515'245u + 12'345u;
        const double centeredNoise =
            static_cast<double>(noiseWord & 0xffffu) / 32767.5 - 1.0;
        const float snare = snarePhase < 0.025
            ? static_cast<float>(0.35 * centeredNoise * std::exp(-80.0 * snarePhase))
            : 0.0f;

        const float sweep = static_cast<float>(
            0.15 * std::sin(2.0 * std::numbers::pi * (200.0 + 8'000.0 * t / seconds) * t));

        channels[0][static_cast<std::size_t>(i)] = kick + mid + hi + snare + sweep;
        channels[1][static_cast<std::size_t>(i)] =
            kick * 0.9f + mid + hi * 0.7f - snare * 0.3f + sweep * 0.8f;
    }

    return channels;
}

Audio scale(Audio input, float gain)
{
    for (auto& channel : input)
        for (auto& sample : channel)
            sample *= gain;
    return input;
}

Audio makeStereoFromMono(const std::vector<float>& mono)
{
    return Audio {mono, mono};
}

Audio makeHotSine(double sampleRate, double seconds, double hz)
{
    const int frames = static_cast<int>(sampleRate * seconds);
    std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        mono[static_cast<std::size_t>(i)] =
            static_cast<float>(1.5 * std::sin(2.0 * std::numbers::pi * hz * t));
    }
    return makeStereoFromMono(mono);
}

Audio makePulseTrain(double sampleRate, double seconds)
{
    const int frames = static_cast<int>(sampleRate * seconds);
    const int period = static_cast<int>(sampleRate * 0.025);
    const int width = std::max(8, static_cast<int>(sampleRate * 0.001));
    std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);

    for (int i = 0; i < frames; ++i) {
        const int p = i % period;
        if (p < width) {
            const double phase = static_cast<double>(p) / static_cast<double>(width);
            const double taper = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase);
            mono[static_cast<std::size_t>(i)] = static_cast<float>(1.8 * taper);
        }
    }

    return makeStereoFromMono(mono);
}

Audio makeKickish(double sampleRate, double seconds)
{
    const int frames = static_cast<int>(sampleRate * seconds);
    std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);

    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double phase = std::fmod(t, 0.2);
        if (phase > 0.11)
            continue;

        const double pitch = 48.0 + 90.0 * std::exp(-45.0 * phase);
        const double body =
            1.75 * std::exp(-28.0 * phase)
            * std::sin(2.0 * std::numbers::pi * pitch * phase);
        const double click = phase < 0.003 ? 0.55 * (1.0 - phase / 0.003) : 0.0;
        mono[static_cast<std::size_t>(i)] = static_cast<float>(body + click);
    }

    return makeStereoFromMono(mono);
}

std::vector<float> makeSine(double sampleRate, double seconds, double hz, double amplitude)
{
    const int frames = static_cast<int>(sampleRate * seconds);
    std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        mono[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * t));
    }
    return mono;
}

double rmsDb(const std::vector<float>& samples)
{
    double energy = 0.0;
    for (float sample : samples)
        energy += static_cast<double>(sample) * static_cast<double>(sample);

    const double mean = samples.empty() ? 0.0 : energy / static_cast<double>(samples.size());
    return mean > 1.0e-24 ? mastering::dsp::gainToDb(std::sqrt(mean)) : -120.0;
}

Audio loudnessMatch(Audio wet, const Audio& dry)
{
    if (wet.empty() || dry.empty())
        return wet;

    const double target = rmsDb(dry.front());
    const double current = rmsDb(wet.front());
    if (current < -80.0)
        return wet;

    const float gain = static_cast<float>(mastering::dsp::dbToGain(target - current));
    for (auto& channel : wet)
        for (auto& sample : channel)
            sample *= gain;
    return wet;
}

double truePeakDb(const Audio& channels, double sampleRate)
{
    if (channels.empty() || channels.front().empty())
        return -120.0;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sampleRate, static_cast<int>(channels.size()), 4096);

    std::vector<const float*> ptrs(channels.size(), nullptr);
    constexpr int hop = 2048;
    for (std::size_t offset = 0; offset < channels.front().size();
         offset += static_cast<std::size_t>(hop)) {
        const auto count = static_cast<int>(
            std::min(channels.front().size() - offset, static_cast<std::size_t>(hop)));
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            ptrs[channel] = channels[channel].data() + offset;
        meter.process(ptrs.data(), static_cast<int>(channels.size()), count);
    }

    meter.finalize();
    const auto linear = meter.snapshot().truePeakLinear;
    return linear > 1.0e-12 ? mastering::dsp::gainToDb(linear) : -120.0;
}

Audio processChain(
    Audio input,
    mastering::dsp::MasterSafetySettings settings,
    int factor,
    double sampleRate,
    mastering::dsp::MasterSafetyMeters* metersOut = nullptr)
{
    const int channelCount = static_cast<int>(input.size());
    settings.saturation.oversamplingFactor = factor;
    settings.softClip.oversamplingFactor = factor;
    settings.hardClip.oversamplingFactor = factor;
    settings.limiter.oversamplingFactor = factor;

    mastering::dsp::MasterSafetyChain chain;
    chain.prepare(sampleRate, 2048, channelCount, factor);
    chain.setSettings(settings);
    // Topology-affecting settings (OS factor and limiter look-ahead) are applied on prepare().
    chain.prepare(sampleRate, 2048, channelCount, factor);

    const int frames = static_cast<int>(input.front().size());
    std::vector<float*> ptrs(static_cast<std::size_t>(channelCount), nullptr);
    for (int offset = 0; offset < frames; offset += 512) {
        const int count = std::min(512, frames - offset);
        for (int channel = 0; channel < channelCount; ++channel)
            ptrs[static_cast<std::size_t>(channel)] =
                input[static_cast<std::size_t>(channel)].data() + offset;
        chain.process(ptrs.data(), channelCount, count);
    }

    const int flushFrames = chain.latencySamples() + 128;
    Audio flush(
        static_cast<std::size_t>(channelCount),
        std::vector<float>(static_cast<std::size_t>(flushFrames), 0.0f));
    for (int channel = 0; channel < channelCount; ++channel)
        ptrs[static_cast<std::size_t>(channel)] = flush[static_cast<std::size_t>(channel)].data();
    chain.finalize(ptrs.data(), channelCount, flushFrames);

    for (int channel = 0; channel < channelCount; ++channel) {
        auto& dst = input[static_cast<std::size_t>(channel)];
        const auto& src = flush[static_cast<std::size_t>(channel)];
        dst.insert(dst.end(), src.begin(), src.end());
    }

    const auto latency = static_cast<std::size_t>(std::max(0, chain.latencySamples()));
    if (input.front().size() > latency) {
        for (auto& channel : input)
            channel.erase(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(latency));
    }

    if (metersOut != nullptr)
        *metersOut = chain.meters();

    return input;
}

Audio processLimiter(
    Audio input,
    double ceilingDb,
    double sampleRate,
    mastering::dsp::TruePeakLimiterMeters* metersOut = nullptr,
    std::vector<float>* gainEnvelopeOut = nullptr,
    double releaseMs = 50.0,
    int factor = 4)
{
    const int channelCount = static_cast<int>(input.size());

    mastering::dsp::TruePeakLimiter limiter;
    mastering::dsp::TruePeakLimiterSettings settings;
    settings.ceilingDbTp = ceilingDb;
    settings.lookAheadMs = 2.0;
    settings.releaseMs = releaseMs;
    settings.oversamplingFactor = factor;
    settings.stereoLink = true;

    limiter.prepare(sampleRate, 2048, channelCount, factor);
    limiter.setSettings(settings);
    // Required for topology changes requested by setSettings().
    limiter.prepare(sampleRate, 2048, channelCount, factor);

    if (gainEnvelopeOut != nullptr) {
        const auto reserve = (input.front().size()
                                + static_cast<std::size_t>(limiter.latencySamples() + 256))
            * static_cast<std::size_t>(std::max(1, factor));
        limiter.reserveGainEnvelope(reserve);
    }

    const int frames = static_cast<int>(input.front().size());
    std::vector<float*> ptrs(static_cast<std::size_t>(channelCount), nullptr);
    for (int offset = 0; offset < frames; offset += 512) {
        const int count = std::min(512, frames - offset);
        for (int channel = 0; channel < channelCount; ++channel)
            ptrs[static_cast<std::size_t>(channel)] =
                input[static_cast<std::size_t>(channel)].data() + offset;
        limiter.process(ptrs.data(), channelCount, count);
    }

    const int flushFrames = limiter.latencySamples() + 128;
    Audio flush(
        static_cast<std::size_t>(channelCount),
        std::vector<float>(static_cast<std::size_t>(flushFrames), 0.0f));
    for (int channel = 0; channel < channelCount; ++channel)
        ptrs[static_cast<std::size_t>(channel)] = flush[static_cast<std::size_t>(channel)].data();
    limiter.finalize(ptrs.data(), channelCount, flushFrames);

    for (int channel = 0; channel < channelCount; ++channel) {
        auto& dst = input[static_cast<std::size_t>(channel)];
        const auto& src = flush[static_cast<std::size_t>(channel)];
        dst.insert(dst.end(), src.begin(), src.end());
    }

    const auto latency = static_cast<std::size_t>(std::max(0, limiter.latencySamples()));
    if (input.front().size() > latency) {
        for (auto& channel : input)
            channel.erase(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(latency));
    }

    if (metersOut != nullptr)
        *metersOut = limiter.meters();

    if (gainEnvelopeOut != nullptr) {
        auto envelope = limiter.gainEnvelopeDb();
        std::size_t lastNonZero = envelope.size();
        while (lastNonZero > 0 && std::abs(envelope[lastNonZero - 1]) < 1.0e-7f)
            --lastNonZero;
        const auto keepTail =
            static_cast<std::size_t>(0.05 * sampleRate * static_cast<double>(std::max(1, factor)));
        envelope.resize(std::min(envelope.size(), lastNonZero + keepTail));
        *gainEnvelopeOut = std::move(envelope);
    }

    return input;
}

void processMonoNonlinear(std::vector<float>& samples, int factor, double sampleRate, const std::string& kind)
{
    if (kind == "sat") {
        mastering::dsp::SaturationProcessor processor;
        mastering::dsp::SaturationSettings settings;
        settings.drive = 8.0;
        settings.mix = 1.0;
        settings.autoGainStatic = true;
        settings.oversamplingFactor = factor;
        processor.prepare(sampleRate, 2048, 1, factor);
        processor.setSettings(settings);
        processor.prepare(sampleRate, 2048, 1, factor);

        for (int offset = 0; offset < static_cast<int>(samples.size()); offset += 512) {
            const int count = std::min(512, static_cast<int>(samples.size()) - offset);
            float* ptr = samples.data() + offset;
            processor.process(&ptr, 1, count);
        }
    } else if (kind == "soft") {
        mastering::dsp::SoftClipper processor;
        mastering::dsp::SoftClipSettings settings;
        settings.thresholdDb = -12.0;
        settings.kneeDb = 6.0;
        settings.driveDb = 18.0;
        settings.mix = 1.0;
        settings.oversamplingFactor = factor;
        processor.prepare(sampleRate, 2048, 1, factor);
        processor.setSettings(settings);
        processor.prepare(sampleRate, 2048, 1, factor);

        for (int offset = 0; offset < static_cast<int>(samples.size()); offset += 512) {
            const int count = std::min(512, static_cast<int>(samples.size()) - offset);
            float* ptr = samples.data() + offset;
            processor.process(&ptr, 1, count);
        }
    } else {
        mastering::dsp::HardClipper processor;
        mastering::dsp::HardClipSettings settings;
        settings.ceilingDb = -4.0;
        settings.driveDb = 16.0;
        settings.oversamplingFactor = factor;
        processor.prepare(sampleRate, 2048, 1, factor);
        processor.setSettings(settings);
        processor.prepare(sampleRate, 2048, 1, factor);

        for (int offset = 0; offset < static_cast<int>(samples.size()); offset += 512) {
            const int count = std::min(512, static_cast<int>(samples.size()) - offset);
            float* ptr = samples.data() + offset;
            processor.process(&ptr, 1, count);
        }
    }
}

Spectrum spectrum4096Hann(const std::vector<float>& samples, double sampleRate)
{
    constexpr std::size_t n = 4096;
    Spectrum spectrum;
    spectrum.magnitude.assign(n / 2 + 1, 0.0);
    spectrum.binHz = sampleRate / static_cast<double>(n);
    if (samples.size() < n)
        return spectrum;

    const std::size_t start = std::min(samples.size() - n, samples.size() / 3);
    std::vector<double> re(n, 0.0);
    std::vector<double> im(n, 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        const double window =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
        re[i] = static_cast<double>(samples[start + i]) * window;
    }

    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const double wLenRe = std::cos(angle);
        const double wLenIm = std::sin(angle);

        for (std::size_t i = 0; i < n; i += len) {
            double wRe = 1.0;
            double wIm = 0.0;
            for (std::size_t j = 0; j < len / 2; ++j) {
                const double uRe = re[i + j];
                const double uIm = im[i + j];
                const double vRe = re[i + j + len / 2] * wRe - im[i + j + len / 2] * wIm;
                const double vIm = re[i + j + len / 2] * wIm + im[i + j + len / 2] * wRe;
                re[i + j] = uRe + vRe;
                im[i + j] = uIm + vIm;
                re[i + j + len / 2] = uRe - vRe;
                im[i + j + len / 2] = uIm - vIm;
                const double nextWRe = wRe * wLenRe - wIm * wLenIm;
                wIm = wRe * wLenIm + wIm * wLenRe;
                wRe = nextWRe;
            }
        }
    }

    for (std::size_t k = 0; k <= n / 2; ++k)
        spectrum.magnitude[k] = std::hypot(re[k], im[k]);

    return spectrum;
}

bool isExpectedFundamentalOrOddHarmonic(int bin, double binHz, double fundamentalHz, double sampleRate)
{
    for (int harmonic = 1; harmonic < 99; harmonic += 2) {
        const double frequency = fundamentalHz * static_cast<double>(harmonic);
        if (frequency >= sampleRate * 0.5)
            break;
        const int expectedBin = static_cast<int>(std::lround(frequency / binHz));
        if (std::abs(bin - expectedBin) <= 2)
            return true;
    }
    return false;
}

double aliasingMetric(const Spectrum& spectrum, double fundamentalHz, double sampleRate)
{
    double total = 0.0;
    double spurious = 0.0;

    for (std::size_t bin = 1; bin + 1 < spectrum.magnitude.size(); ++bin) {
        const double energy = spectrum.magnitude[bin] * spectrum.magnitude[bin];
        total += energy;
        if (!isExpectedFundamentalOrOddHarmonic(
                static_cast<int>(bin),
                spectrum.binHz,
                fundamentalHz,
                sampleRate))
            spurious += energy;
    }

    return total > 1.0e-24 ? spurious / total : 0.0;
}

std::string nullRejectionDb(double errorEnergy, double referenceEnergy)
{
    if (errorEnergy <= 1.0e-30)
        return "-inf";
    if (referenceEnergy <= 1.0e-30)
        return "n/a";
    return fixed(10.0 * std::log10(errorEnergy / referenceEnergy), 2);
}

std::pair<double, double> delayedNullEnergy(
    const std::vector<float>& dry,
    const std::vector<float>& wet,
    int delay,
    int settle = 64)
{
    double errorEnergy = 0.0;
    double referenceEnergy = 0.0;
    const int start = std::max(0, delay + settle);
    const int end = static_cast<int>(std::min(dry.size(), wet.size()));
    for (int i = start; i < end; ++i) {
        const double reference = dry[static_cast<std::size_t>(i - delay)];
        const double error = static_cast<double>(wet[static_cast<std::size_t>(i)]) - reference;
        errorEnergy += error * error;
        referenceEnergy += reference * reference;
    }
    return {errorEnergy, referenceEnergy};
}

int peakIndex(const std::vector<float>& samples)
{
    int index = 0;
    double peak = -1.0;
    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        const double absSample = std::abs(static_cast<double>(samples[static_cast<std::size_t>(i)]));
        if (absSample > peak) {
            peak = absSample;
            index = i;
        }
    }
    return index;
}

std::string machineArchitecture()
{
#if defined(__unix__) || defined(__APPLE__)
    std::array<char, 128> buffer {};
    std::string result;
    FILE* pipe = popen("uname -m", "r");
    if (pipe != nullptr) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            result += buffer.data();
        pclose(pipe);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    if (!result.empty())
        return result;
#endif

#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "unknown";
#endif
}

void writeGainEnvelopeJson(const fs::path& path, const std::vector<float>& envelope)
{
    std::ofstream out(path);
    out << "[\n";
    for (std::size_t i = 0; i < envelope.size(); ++i) {
        out << "  " << fixed(envelope[i], 6);
        if (i + 1 < envelope.size())
            out << ",";
        out << "\n";
    }
    out << "]\n";
}

void writeAliasingSpectraJson(
    const fs::path& path,
    const Spectrum& sat1x,
    const Spectrum& sat4x)
{
    std::ofstream out(path);
    out << "{\n"
        << "  \"metric\": \"4096-point Hann FFT on 9 kHz sine; spurious/total energy excludes "
           "fundamental plus in-band odd harmonics (±2 bins)\",\n"
        << "  \"sample_rate\": " << fixed(kDefaultSampleRate, 1) << ",\n"
        << "  \"fft_size\": 4096,\n"
        << "  \"bins\": [\n";

    for (std::size_t bin = 0; bin < 64 && bin < sat1x.magnitude.size() && bin < sat4x.magnitude.size();
         ++bin) {
        out << "    {\"bin\": " << bin << ", \"hz\": " << fixed(static_cast<double>(bin) * sat1x.binHz, 3)
            << ", \"sat_1x\": " << scientific(sat1x.magnitude[bin])
            << ", \"sat_4x\": " << scientific(sat4x.magnitude[bin]) << "}";
        if (bin + 1 < 64)
            out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
}

void copyTree(const fs::path& src, const fs::path& dst)
{
    fs::create_directories(dst);
    for (const auto& entry : fs::recursive_directory_iterator(src)) {
        const auto rel = fs::relative(entry.path(), src);
        const auto target = dst / rel;
        if (entry.is_directory()) {
            fs::create_directories(target);
        } else if (entry.is_regular_file()) {
            fs::create_directories(target.parent_path());
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    fs::path outDir = "artifacts/dsp_validation";
    if (argc > 1)
        outDir = argv[1];

    if (fs::exists(outDir) && outDir.filename() == "dsp_validation")
        fs::remove_all(outDir);
    fs::create_directories(outDir);
    fs::create_directories(outDir / "loudness_matched");

    std::vector<WavRecord> wavs;

    const auto dry = makeDry(kDefaultSampleRate, 2.0);
    const auto dense = scale(dry, 2.0f);
    writeRecordedWav(wavs, outDir / "dry.wav", dry, kDefaultSampleRate);

    auto renderChainWav = [&](mastering::dsp::MasterSafetySettings settings,
                              int factor,
                              const std::string& name,
                              const Audio& source,
                              mastering::dsp::MasterSafetyMeters* metersOut = nullptr) {
        auto wet = processChain(source, settings, factor, kDefaultSampleRate, metersOut);
        writeRecordedWav(wavs, outDir / name, wet, kDefaultSampleRate);
        writeRecordedWav(
            wavs,
            outDir / "loudness_matched" / name,
            loudnessMatch(wet, dry),
            kDefaultSampleRate);
        return wet;
    };

    {
        mastering::dsp::MasterSafetySettings settings;
        settings.saturationEnabled = true;
        settings.saturation.drive = 4.0;
        settings.saturation.mix = 1.0;
        settings.clipMode = mastering::dsp::ClipMode::none;
        settings.limiterEnabled = false;
        renderChainWav(settings, 1, "saturation_1x.wav", dry);
        renderChainWav(settings, 4, "saturation_4x.wav", dry);
    }

    {
        mastering::dsp::MasterSafetySettings settings;
        settings.saturationEnabled = false;
        settings.clipMode = mastering::dsp::ClipMode::soft;
        settings.softClip.thresholdDb = -1.0;
        settings.softClip.kneeDb = 3.0;
        settings.softClip.driveDb = 6.0;
        settings.limiterEnabled = false;
        renderChainWav(settings, 1, "soft_clip_1x.wav", dry);
        renderChainWav(settings, 4, "soft_clip_4x.wav", dry);
    }

    {
        mastering::dsp::MasterSafetySettings settings;
        settings.saturationEnabled = false;
        settings.clipMode = mastering::dsp::ClipMode::hard;
        settings.hardClip.ceilingDb = -1.0;
        settings.hardClip.driveDb = 6.0;
        settings.limiterEnabled = false;
        renderChainWav(settings, 1, "hard_clip_1x.wav", dry);
        renderChainWav(settings, 4, "hard_clip_4x.wav", dry);
    }

    mastering::dsp::MasterSafetyMeters limiterMinusOneMeters {};
    Audio limiterMinusOneRender;
    for (double ceiling : {-0.1, -1.0}) {
        mastering::dsp::MasterSafetySettings settings;
        settings.saturationEnabled = false;
        settings.clipMode = mastering::dsp::ClipMode::none;
        settings.limiterEnabled = true;
        settings.limiter.ceilingDbTp = ceiling;
        settings.limiter.lookAheadMs = 2.0;
        settings.limiter.releaseMs = 50.0;
        settings.limiter.oversamplingFactor = 4;

        mastering::dsp::MasterSafetyMeters meters {};
        const std::string name = ceiling > -0.5 ? "limiter_minus_0_1.wav" : "limiter_minus_1_0.wav";
        auto render = renderChainWav(settings, 4, name, dense, &meters);
        if (ceiling < -0.5) {
            limiterMinusOneRender = render;
            limiterMinusOneMeters = meters;
        }
    }

    std::vector<CeilingRow> ceilingRows;
    const std::vector<std::pair<std::string, Audio>> ceilingSignals {
        {"dense", dense},
        {"hot sine 1k", makeHotSine(kDefaultSampleRate, 0.75, 1'000.0)},
        {"pulse train", makePulseTrain(kDefaultSampleRate, 0.75)},
        {"synthetic kick-ish", makeKickish(kDefaultSampleRate, 0.9)},
    };

    for (const auto& [signalName, signal] : ceilingSignals) {
        for (double ceiling : {-0.1, -0.3, -0.5, -1.0, -2.0}) {
            mastering::dsp::TruePeakLimiterMeters meters {};
            const auto limited =
                processLimiter(signal, ceiling, kDefaultSampleRate, &meters, nullptr, 50.0, 4);
            const double tp = truePeakDb(limited, kDefaultSampleRate);
            const double error = tp - ceiling;
            ceilingRows.push_back(CeilingRow {
                signalName,
                ceiling,
                tp,
                error,
                meters.maxGainReductionDb,
                meters.safetyClampActivationCount,
                error <= kCeilingOvershootToleranceDb && error >= -kCeilingUndershootToleranceDb,
            });
        }
    }

    {
        std::ostringstream md;
        md << "# Ceiling accuracy report\n\n"
           << "Declared criteria before rendering: overshoot <= " << fixed(kCeilingOvershootToleranceDb, 2)
           << " dB and undershoot <= " << fixed(kCeilingUndershootToleranceDb, 2)
           << " dB. Limiter reconstruction guard is documented as "
           << fixed(kDocumentedReconstructionGuardDb, 2) << " dB, not hidden -1 dB headroom.\n\n"
           << "| Signal | Ceiling | Output TP | Error | Max GR | Clamp activations | Result |\n"
           << "|---|---:|---:|---:|---:|---:|---|\n";
        for (const auto& row : ceilingRows) {
            md << "| " << row.signal << " | " << fixed(row.ceilingDb, 2) << " | "
               << fixed(row.truePeakDb, 3) << " | " << fixed(row.errorDb, 3) << " | "
               << fixed(row.maxGrDb, 3) << " | " << row.clampActivations << " | "
               << passFail(row.pass) << " |\n";
        }
        std::ofstream(outDir / "ceiling_accuracy_report.md") << md.str();
    }

    {
        std::ostringstream md;
        md << "# Limiter report\n\n"
           << "Ceiling matrix uses TruePeakLimiter with prepare -> setSettings -> prepare. "
           << "Meters are captured from lim.meters(), including safetyClampActivationCount. "
           << "Two-sided result criteria: +" << fixed(kCeilingOvershootToleranceDb, 2)
           << " / -" << fixed(kCeilingUndershootToleranceDb, 2) << " dB.\n\n"
           << "| Signal | Ceiling | Output TP | Error | Max GR | Clamp activations | Result |\n"
           << "|---|---:|---:|---:|---:|---:|---|\n";
        for (const auto& row : ceilingRows) {
            md << "| " << row.signal << " | " << fixed(row.ceilingDb, 2) << " | "
               << fixed(row.truePeakDb, 3) << " | " << fixed(row.errorDb, 3) << " | "
               << fixed(row.maxGrDb, 3) << " | " << row.clampActivations << " | "
               << passFail(row.pass) << " |\n";
        }
        std::ofstream(outDir / "limiter_report.md") << md.str();
    }

    {
        std::ostringstream md;
        md << "# Latency report\n\n"
           << "Result requires measured impulse peak index to exactly match reported latency. "
           << "Null rejection compares processed output with delayed dry reference after settle.\n\n"
           << "| Processor | OS | Reported | Measured | Fractional residual | Null rejection | Result |\n"
           << "|---|---:|---:|---:|---:|---:|---|\n";

        for (int factor : {1, 2, 4, 8}) {
            mastering::dsp::Oversampler oversampler;
            oversampler.prepare(kDefaultSampleRate, 4096, 1, factor);

            std::vector<float> impulse(4096, 0.0f);
            impulse[0] = 1.0f;
            float* impulsePtr = impulse.data();
            oversampler.process(&impulsePtr, 1, static_cast<int>(impulse.size()), [](float* const*, int, int, double) {});
            const int measured = peakIndex(impulse);
            const int reported = oversampler.latencySamplesBaseRate();

            auto dryNull = makeSine(kDefaultSampleRate, 4096.0 / kDefaultSampleRate, 300.0, 0.25);
            auto wetNull = dryNull;
            oversampler.reset();
            float* wetPtr = wetNull.data();
            oversampler.process(&wetPtr, 1, static_cast<int>(wetNull.size()), [](float* const*, int, int, double) {});
            const auto [errorEnergy, referenceEnergy] = delayedNullEnergy(dryNull, wetNull, reported);

            md << "| Oversampler | " << factor << " | " << reported << " | " << measured << " | "
               << fixed(oversampler.fractionalLatencyResidual(), 6) << " | "
               << nullRejectionDb(errorEnergy, referenceEnergy) << " dB | "
               << passFail(measured == reported) << " |\n";
        }

        {
            mastering::dsp::TruePeakLimiter limiter;
            mastering::dsp::TruePeakLimiterSettings settings;
            settings.ceilingDbTp = -0.1;
            settings.lookAheadMs = 2.0;
            settings.releaseMs = 50.0;
            settings.oversamplingFactor = 4;
            limiter.prepare(kDefaultSampleRate, 4096, 1, 4);
            limiter.setSettings(settings);
            limiter.prepare(kDefaultSampleRate, 4096, 1, 4);

            std::vector<float> impulse(8192, 0.0f);
            impulse[0] = 0.5f;
            float* ptr = impulse.data();
            limiter.process(&ptr, 1, static_cast<int>(impulse.size()));
            const int measured = peakIndex(impulse);
            const int reported = limiter.latencySamples();

            mastering::dsp::TruePeakLimiter bypassLimiter;
            settings.bypass = true;
            bypassLimiter.prepare(kDefaultSampleRate, 4096, 1, 4);
            bypassLimiter.setSettings(settings);
            bypassLimiter.prepare(kDefaultSampleRate, 4096, 1, 4);
            auto dryNull = makeSine(kDefaultSampleRate, 8192.0 / kDefaultSampleRate, 300.0, 0.25);
            auto wetNull = dryNull;
            float* wetPtr = wetNull.data();
            bypassLimiter.process(&wetPtr, 1, static_cast<int>(wetNull.size()));
            const auto [errorEnergy, referenceEnergy] =
                delayedNullEnergy(dryNull, wetNull, bypassLimiter.latencySamples());

            md << "| TruePeakLimiter | 4 | " << reported << " | " << measured
               << " | n/a | " << nullRejectionDb(errorEnergy, referenceEnergy) << " dB | "
               << passFail(measured == reported) << " |\n";
        }

        std::ofstream(outDir / "latency_report.md") << md.str();
    }

    {
        std::vector<AliasingResult> results;
        Spectrum sat1xSpectrum;
        Spectrum sat4xSpectrum;
        for (const auto& processor : std::vector<std::pair<std::string, std::string>> {
                 {"saturation", "sat"},
                 {"soft_clip", "soft"},
                 {"hard_clip", "hard"},
             }) {
            double baseline = 0.0;
            for (int factor : {1, 2, 4, 8}) {
                auto signal = makeSine(kDefaultSampleRate, 1.0, 9'000.0, 0.9);
                processMonoNonlinear(signal, factor, kDefaultSampleRate, processor.second);
                const auto spectrum = spectrum4096Hann(signal, kDefaultSampleRate);
                const auto metric = aliasingMetric(spectrum, 9'000.0, kDefaultSampleRate);
                if (factor == 1)
                    baseline = metric;
                if (processor.second == "sat" && factor == 1)
                    sat1xSpectrum = spectrum;
                if (processor.second == "sat" && factor == 4)
                    sat4xSpectrum = spectrum;

                std::string settings = "drive=8";
                if (processor.second == "soft")
                    settings = "threshold=-12 dB, knee=6 dB, drive=18 dB";
                else if (processor.second == "hard")
                    settings = "ceiling=-4 dB, drive=16 dB";

                results.push_back(AliasingResult {
                    processor.first,
                    factor,
                    settings,
                    metric,
                    metric - baseline,
                    factor == 1 ? "baseline" : (metric < baseline ? "PASS" : "CHECK"),
                });
            }
        }

        std::ostringstream md;
        md << "# Aliasing report\n\n"
           << "Metric definition: render a 9 kHz sine at 48 kHz, analyse a steady-state "
              "4096-point Hann-window FFT, sum total in-band bin energy excluding DC, then "
              "divide spurious energy by total energy. Spurious energy is every in-band bin "
              "except the fundamental and expected odd harmonics below Nyquist, each excluded "
              "with a ±2-bin guard. Lower values indicate less folded or unrelated spectral "
              "energy.\n\n"
           << "Soft clipping uses SoftClipper::transfer with a soft knee. Hard clipping uses a "
              "hard clamp. Their drive/threshold settings are intentionally different so the "
              "metrics exercise different nonlinear transfer curves.\n\n"
           << "| Processor | OS | Settings | Spurious/total | Delta vs 1x | Result |\n"
           << "|---|---:|---|---:|---:|---|\n";
        for (const auto& result : results) {
            md << "| " << result.processor << " | " << result.factor << " | " << result.settings
               << " | " << scientific(result.metric) << " | " << scientific(result.deltaVs1x)
               << " | " << result.result << " |\n";
        }
        std::ofstream(outDir / "aliasing_report.md") << md.str();
        writeAliasingSpectraJson(outDir / "aliasing_spectra.json", sat1xSpectrum, sat4xSpectrum);
    }

    {
        auto transient = makePulseTrain(kDefaultSampleRate, 0.02);
        transient[0].resize(static_cast<std::size_t>(kDefaultSampleRate * 1.2), 0.0f);
        transient[1].resize(static_cast<std::size_t>(kDefaultSampleRate * 1.2), 0.0f);
        transient[0][static_cast<std::size_t>(0.1 * kDefaultSampleRate)] = 2.0f;
        transient[1][static_cast<std::size_t>(0.1 * kDefaultSampleRate)] = 2.0f;

        mastering::dsp::TruePeakLimiterMeters meters {};
        std::vector<float> envelope;
        (void) processLimiter(transient, -1.0, kDefaultSampleRate, &meters, &envelope, 120.0, 4);

        const auto maxIt = std::max_element(envelope.begin(), envelope.end());
        const double maxGr = maxIt != envelope.end() ? *maxIt : 0.0;
        const std::size_t maxIndex =
            maxIt != envelope.end() ? static_cast<std::size_t>(std::distance(envelope.begin(), maxIt)) : 0U;
        const auto findRecovery = [&](double remainingFraction) {
            if (maxGr <= 1.0e-9)
                return 0.0;
            for (std::size_t i = maxIndex; i < envelope.size(); ++i) {
                if (envelope[i] <= maxGr * remainingFraction) {
                    return 1000.0 * static_cast<double>(i - maxIndex)
                        / (kDefaultSampleRate * 4.0);
                }
            }
            return std::numeric_limits<double>::quiet_NaN();
        };

        const double recovery90Ms = findRecovery(0.10);
        const double recovery99Ms = findRecovery(0.01);

        std::ofstream(outDir / "release_pumping_report.md")
            << "# Release pumping report\n\n"
            << "Stimulus: one full-scale transient through TruePeakLimiter with reserveGainEnvelope(). "
            << "Recovery time is measured from maximum gain reduction until remaining GR is <= 10% "
               "(90% recovered) and <= 1% (99% recovered).\n\n"
            << "| Max GR | 90% recovery ms | 99% recovery ms | Max per-sample gain jump | Result |\n"
            << "|---:|---:|---:|---:|---|\n"
            << "| " << fixed(maxGr, 3) << " | " << fixed(recovery90Ms, 3) << " | "
            << fixed(recovery99Ms, 3) << " | " << scientific(meters.maxPerSampleGainJump)
            << " | smoke |\n";

        writeGainEnvelopeJson(outDir / "gr_envelope.json", envelope);
    }

    {
        mastering::dsp::TruePeakLimiter limiter;
        mastering::dsp::TruePeakLimiterSettings settings;
        settings.ceilingDbTp = -1.0;
        settings.lookAheadMs = 2.0;
        settings.releaseMs = 80.0;
        settings.oversamplingFactor = 4;
        settings.stereoLink = true;
        limiter.prepare(kDefaultSampleRate, 2048, 2, 4);
        limiter.setSettings(settings);
        limiter.prepare(kDefaultSampleRate, 2048, 2, 4);

        std::vector<float> left(4096, 0.1f);
        std::vector<float> right(4096, 0.1f);
        left[1500] = 2.0f;
        float* channels[2] {left.data(), right.data()};
        limiter.process(channels, 2, static_cast<int>(left.size()));

        double meanAbsDiff = 0.0;
        double maxAbsDiff = 0.0;
        int count = 0;
        for (int i = 400; i < 1400; ++i) {
            const double diff = std::abs(
                static_cast<double>(left[static_cast<std::size_t>(i)])
                - static_cast<double>(right[static_cast<std::size_t>(i)]));
            meanAbsDiff += diff;
            maxAbsDiff = std::max(maxAbsDiff, diff);
            ++count;
        }
        meanAbsDiff /= static_cast<double>(std::max(1, count));

        std::ofstream(outDir / "stereo_link_report.md")
            << "# Stereo link report\n\n"
            << "Stimulus: left-only peak with both channels otherwise held at 0.1. "
               "Steady-state match is measured before the left-only peak content while linked "
               "gain is active/aligned.\n\n"
            << "| L/R steady-state mean abs diff | L/R steady-state max abs diff | Result |\n"
            << "|---:|---:|---|\n"
            << "| " << scientific(meanAbsDiff) << " | " << scientific(maxAbsDiff) << " | "
            << passFail(meanAbsDiff < 1.0e-3) << " |\n";
    }

    {
        if (limiterMinusOneRender.empty()) {
            mastering::dsp::MasterSafetySettings settings;
            settings.limiterEnabled = true;
            settings.limiter.ceilingDbTp = -1.0;
            settings.limiter.oversamplingFactor = 4;
            limiterMinusOneRender =
                processChain(dense, settings, 4, kDefaultSampleRate, &limiterMinusOneMeters);
        }

        const auto report = mastering::dsp::ExportQc::analyse(
            limiterMinusOneRender,
            kDefaultSampleRate,
            32,
            limiterMinusOneMeters,
            0,
            -1.0,
            0.10,
            nullptr);
        std::ofstream(outDir / "qc_report.json") << mastering::dsp::ExportQc::toJson(report);
        std::ofstream(outDir / "qc_report.md")
            << mastering::dsp::ExportQc::toMarkdown(report)
            << "\nTechnical/advisory split check: technical=" << qcStatusName(report.technicalStatus)
            << ", advisory=" << qcStatusName(report.advisoryStatus)
            << " with styleProfile=nullptr, so LUFS does not create an advisory warning.\n";
    }

    {
        struct Scenario {
            double sampleRate;
            int block;
            int factor;
        };

        std::ostringstream md;
        md << "# Performance report\n\n"
           << "RTF is defined as processed_audio_seconds / wall_clock_seconds; higher is faster "
              "than realtime.\n\n"
           << "- Hardware (uname -m): " << machineArchitecture() << "\n\n"
           << "| Processor | SR | Block | OS | processed_s | wall_s | RTF | result |\n"
           << "|---|---:|---:|---:|---:|---:|---:|---|\n";

        for (const auto scenario : std::vector<Scenario> {
                 {48'000.0, 128, 4},
                 {48'000.0, 512, 4},
                 {96'000.0, 128, 4},
                 {96'000.0, 512, 4},
                 {48'000.0, 128, 8},
             }) {
            mastering::dsp::MasterSafetySettings settings;
            settings.saturationEnabled = true;
            settings.saturation.drive = 3.0;
            settings.clipMode = mastering::dsp::ClipMode::soft;
            settings.softClip.thresholdDb = -2.0;
            settings.softClip.driveDb = 5.0;
            settings.limiterEnabled = true;
            settings.limiter.ceilingDbTp = -1.0;
            settings.saturation.oversamplingFactor = scenario.factor;
            settings.softClip.oversamplingFactor = scenario.factor;
            settings.hardClip.oversamplingFactor = scenario.factor;
            settings.limiter.oversamplingFactor = scenario.factor;

            mastering::dsp::MasterSafetyChain chain;
            chain.prepare(scenario.sampleRate, scenario.block, 2, scenario.factor);
            chain.setSettings(settings);
            chain.prepare(scenario.sampleRate, scenario.block, 2, scenario.factor);

            std::vector<float> left(static_cast<std::size_t>(scenario.block), 0.0f);
            std::vector<float> right(static_cast<std::size_t>(scenario.block), 0.0f);
            const double targetProcessedSeconds = 0.25;
            const int iterations = std::max(
                1,
                static_cast<int>(std::ceil(targetProcessedSeconds * scenario.sampleRate
                    / static_cast<double>(scenario.block))));

            const auto start = std::chrono::steady_clock::now();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                for (int i = 0; i < scenario.block; ++i) {
                    const double t = static_cast<double>(iteration * scenario.block + i) / scenario.sampleRate;
                    left[static_cast<std::size_t>(i)] =
                        static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 220.0 * t));
                    right[static_cast<std::size_t>(i)] =
                        static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 330.0 * t));
                }
                float* ptrs[2] {left.data(), right.data()};
                chain.process(ptrs, 2, scenario.block);
            }
            const auto stop = std::chrono::steady_clock::now();
            const double wallSeconds = std::chrono::duration<double>(stop - start).count();
            const double processedSeconds =
                static_cast<double>(iterations * scenario.block) / scenario.sampleRate;
            const double rtf = wallSeconds > 0.0 ? processedSeconds / wallSeconds : 0.0;

            md << "| MasterSafetyChain | " << fixed(scenario.sampleRate, 0) << " | "
               << scenario.block << " | " << scenario.factor << " | "
               << fixed(processedSeconds, 3) << " | " << fixed(wallSeconds, 6) << " | "
               << fixed(rtf, 2) << " | smoke |\n";
        }

        std::ofstream(outDir / "performance_report.md") << md.str();
    }

    {
        std::ostringstream md;
        md << "# Integrity report\n\n"
           << "Hashes use FNV-1a 64-bit over the written WAV bytes (labelled fnv1a64).\n\n"
           << "| WAV | duration | sr | channels | finite | fnv1a64 | Result |\n"
           << "|---|---:|---:|---:|---|---:|---|\n";
        for (const auto& wav : wavs) {
            const double duration =
                wav.sampleRate > 0.0 ? static_cast<double>(wav.frames) / wav.sampleRate : 0.0;
            const bool exists = fs::exists(wav.path);
            const bool pass = exists && wav.frames > 0 && wav.channels > 0 && wav.finite;
            md << "| " << fs::relative(wav.path, outDir).generic_string() << " | "
               << fixed(duration, 3) << " | " << fixed(wav.sampleRate, 0) << " | "
               << wav.channels << " | " << (wav.finite ? "true" : "false") << " | "
               << (exists ? fnv1a64Hex(wav.path) : "missing") << " | " << passFail(pass)
               << " |\n";
        }
        std::ofstream(outDir / "integrity_report.md") << md.str();
    }

    std::ofstream(outDir / "listening_checklist.md")
        << "# Manual listening checklist (M1B)\n\n"
        << "Automated tests do **not** prove musical quality. Listen to:\n\n"
        << "1. Transient preservation (kick/snare)\n"
        << "2. Harshness / fizz (aliasing)\n"
        << "3. Limiter pumping on dense program\n"
        << "4. Stereo stability (asymmetric transients)\n"
        << "5. Low-end integrity under saturation + clip\n"
        << "6. Limiter distortion / release behavior\n"
        << "7. Active vs bypass loudness match (loudness_matched/)\n"
        << "8. Compare 1x vs 4x of the same processor\n\n"
        << "Do not call a processor musical or transparent solely because unit tests pass.\n";

    if (fs::exists("/opt/cursor/artifacts"))
        copyTree(outDir, "/opt/cursor/artifacts/dsp_validation");

    std::cout << "Wrote DSP validation artifacts to " << outDir << "\n";
    return 0;
}
