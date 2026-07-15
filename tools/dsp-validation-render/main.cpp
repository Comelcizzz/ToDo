#include "mastering/analysis/LoudnessMeter.h"
#include "mastering/dsp/ExportQc.h"
#include "mastering/dsp/MasterSafetyChain.h"
#include "mastering/dsp/NonlinearProcessors.h"
#include "mastering/dsp/Oversampler.h"
#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void writeWav(const fs::path& path, const std::vector<std::vector<float>>& ch, double sr)
{
    const int channels = static_cast<int>(ch.size());
    const int frames = static_cast<int>(ch.front().size());
    std::ofstream out(path, std::ios::binary);
    auto w32 = [&](uint32_t v) {
        const char b[4] {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
        out.write(b, 4);
    };
    auto w16 = [&](uint16_t v) {
        const char b[2] {char(v), char(v >> 8)};
        out.write(b, 2);
    };
    const uint32_t dataSize = static_cast<uint32_t>(frames * channels * 4);
    out.write("RIFF", 4);
    w32(36 + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    w32(16);
    w16(3); // IEEE float
    w16(static_cast<uint16_t>(channels));
    w32(static_cast<uint32_t>(sr));
    w32(static_cast<uint32_t>(sr * channels * 4));
    w16(static_cast<uint16_t>(channels * 4));
    w16(32);
    out.write("data", 4);
    w32(dataSize);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            const float v = ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
            out.write(reinterpret_cast<const char*>(&v), 4);
        }
}

std::vector<std::vector<float>> makeDry(double sr, double seconds)
{
    const int n = static_cast<int>(sr * seconds);
    std::vector<std::vector<float>> ch(2, std::vector<float>(static_cast<std::size_t>(n), 0.0f));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        const float kick = (i % static_cast<int>(sr * 0.5) < 40)
            ? static_cast<float>(0.9 * std::exp(-40.0 * (t - std::floor(t / 0.5) * 0.5))
                * std::sin(2 * std::numbers::pi * 60 * t))
            : 0.0f;
        const float mid = static_cast<float>(0.35 * std::sin(2 * std::numbers::pi * 1'000 * t));
        const float hi = static_cast<float>(0.2 * std::sin(2 * std::numbers::pi * 12'000 * t));
        const float snare = (i % static_cast<int>(sr * 0.75) < 20)
            ? static_cast<float>(0.5 * ((static_cast<unsigned>(i) * 1103515245u) % 1000) / 1000.0)
            : 0.0f;
        const float sweep = static_cast<float>(
            0.15 * std::sin(2 * std::numbers::pi * (200.0 + 8'000.0 * t / seconds) * t));
        ch[0][static_cast<std::size_t>(i)] = kick + mid + hi + snare + sweep;
        ch[1][static_cast<std::size_t>(i)] = kick * 0.9f + mid + hi * 0.7f - snare * 0.3f + sweep * 0.8f;
    }
    return ch;
}

// DFT magnitude energy excluding fundamental + expected odd harmonics below Nyquist.
// Remaining in-band energy is treated as an aliasing / spurious proxy (lower is better).
double aliasingEnergyRatio(const std::vector<float>& x, double sr, double fundamentalHz)
{
    if (x.size() < 64)
        return 0.0;
    // Use power-of-two window from steady region.
    std::size_t n = 1;
    while ((n << 1) <= x.size() && n < 4096)
        n <<= 1;
    const std::size_t start = std::min(x.size() - n, x.size() / 4);
    std::vector<double> re(n, 0.0), im(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
        re[i] = static_cast<double>(x[start + i]) * w;
    }
    // Radix-2 iterative Cooley–Tukey FFT (in-place).
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const double wlenRe = std::cos(ang);
        const double wlenIm = std::sin(ang);
        for (std::size_t i = 0; i < n; i += len) {
            double wr = 1.0, wi = 0.0;
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto uRe = re[i + j];
                const auto uIm = im[i + j];
                const auto vRe = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                const auto vIm = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j] = uRe + vRe;
                im[i + j] = uIm + vIm;
                re[i + j + len / 2] = uRe - vRe;
                im[i + j + len / 2] = uIm - vIm;
                const auto nwr = wr * wlenRe - wi * wlenIm;
                wi = wr * wlenIm + wi * wlenRe;
                wr = nwr;
            }
        }
    }

    const double binHz = sr / static_cast<double>(n);
    const int nyquistBin = static_cast<int>(n / 2);
    auto nearExpected = [&](int bin) {
        for (int h = 1; h <= 15; h += 2) {
            const double f = fundamentalHz * static_cast<double>(h);
            if (f >= sr * 0.5 - binHz)
                break;
            const int b = static_cast<int>(std::lround(f / binHz));
            if (std::abs(bin - b) <= 2)
                return true;
        }
        return false;
    };

    double total = 0.0;
    double spurious = 0.0;
    for (int k = 1; k < nyquistBin; ++k) {
        const double mag2 = re[static_cast<std::size_t>(k)] * re[static_cast<std::size_t>(k)]
            + im[static_cast<std::size_t>(k)] * im[static_cast<std::size_t>(k)];
        total += mag2;
        if (!nearExpected(k))
            spurious += mag2;
    }
    return total > 1.0e-18 ? spurious / total : 0.0;
}

double truePeakDb(const std::vector<std::vector<float>>& ch, double sr)
{
    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, static_cast<int>(ch.size()), 4096);
    std::vector<const float*> ptrs(ch.size());
    constexpr int hop = 2048;
    for (std::size_t off = 0; off < ch[0].size(); off += static_cast<std::size_t>(hop)) {
        const auto n = static_cast<int>(std::min(ch[0].size() - off, static_cast<std::size_t>(hop)));
        for (std::size_t c = 0; c < ch.size(); ++c)
            ptrs[c] = ch[c].data() + off;
        meter.process(ptrs.data(), static_cast<int>(ch.size()), n);
    }
    meter.finalize();
    const auto lin = meter.snapshot().truePeakLinear;
    return lin > 1.0e-12 ? mastering::dsp::gainToDb(lin) : -120.0;
}

double rmsDb(const std::vector<float>& x)
{
    double acc = 0.0;
    for (float v : x)
        acc += static_cast<double>(v) * v;
    const auto mean = x.empty() ? 0.0 : acc / static_cast<double>(x.size());
    return mean > 1.0e-24 ? mastering::dsp::gainToDb(std::sqrt(mean)) : -120.0;
}

std::vector<std::vector<float>> loudnessMatch(
    std::vector<std::vector<float>> wet,
    const std::vector<std::vector<float>>& dry)
{
    const auto target = rmsDb(dry[0]);
    const auto current = rmsDb(wet[0]);
    if (current < -80.0)
        return wet;
    const auto g = static_cast<float>(
        mastering::dsp::dbToGain(target - current));
    for (auto& ch : wet)
        for (auto& s : ch)
            s *= g;
    return wet;
}

std::vector<std::vector<float>> processChain(
    std::vector<std::vector<float>> x,
    mastering::dsp::MasterSafetySettings settings,
    int factor,
    double sr,
    mastering::dsp::MasterSafetyMeters* metersOut = nullptr)
{
    settings.saturation.oversamplingFactor = factor;
    settings.softClip.oversamplingFactor = factor;
    settings.hardClip.oversamplingFactor = factor;
    settings.limiter.oversamplingFactor = factor;

    mastering::dsp::MasterSafetyChain chain;
    chain.prepare(sr, 2048, 2, factor);
    chain.setSettings(settings);

    const int n = static_cast<int>(x[0].size());
    for (int off = 0; off < n; off += 512) {
        const int m = std::min(512, n - off);
        float* slice[2] {x[0].data() + off, x[1].data() + off};
        chain.process(slice, 2, m);
    }

    std::vector<float> f0(static_cast<std::size_t>(chain.latencySamples() + 64), 0.0f);
    std::vector<float> f1(f0.size(), 0.0f);
    float* fp[2] {f0.data(), f1.data()};
    chain.finalize(fp, 2, static_cast<int>(f0.size()));
    x[0].insert(x[0].end(), f0.begin(), f0.end());
    x[1].insert(x[1].end(), f1.begin(), f1.end());

    // Preserve musical start: trim pure processing latency.
    const auto lat = static_cast<std::size_t>(chain.latencySamples());
    if (x[0].size() > lat) {
        x[0].erase(x[0].begin(), x[0].begin() + static_cast<std::ptrdiff_t>(lat));
        x[1].erase(x[1].begin(), x[1].begin() + static_cast<std::ptrdiff_t>(lat));
    }

    if (metersOut != nullptr)
        *metersOut = chain.meters();
    return x;
}

std::vector<float> makeNearNyquist(double sr, double seconds, double hz)
{
    const int n = static_cast<int>(sr * seconds);
    std::vector<float> x(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        x[static_cast<std::size_t>(i)] =
            static_cast<float>(0.9 * std::sin(2.0 * std::numbers::pi * hz * t));
    }
    return x;
}

void processMonoNonlinear(
    std::vector<float>& x,
    int factor,
    double sr,
    const char* kind)
{
    if (std::string(kind) == "sat") {
        mastering::dsp::SaturationProcessor p;
        mastering::dsp::SaturationSettings s;
        s.drive = 6.0;
        s.mix = 1.0;
        s.oversamplingFactor = factor;
        s.autoGainStatic = true;
        p.prepare(sr, 2048, 1, factor);
        p.setSettings(s);
        for (int off = 0; off < static_cast<int>(x.size()); off += 512) {
            const int m = std::min(512, static_cast<int>(x.size()) - off);
            float* ptr = x.data() + off;
            p.process(&ptr, 1, m);
        }
    } else if (std::string(kind) == "soft") {
        mastering::dsp::SoftClipper p;
        mastering::dsp::SoftClipSettings s;
        s.thresholdDb = -6.0;
        s.kneeDb = 3.0;
        s.driveDb = 12.0;
        s.oversamplingFactor = factor;
        p.prepare(sr, 2048, 1, factor);
        p.setSettings(s);
        for (int off = 0; off < static_cast<int>(x.size()); off += 512) {
            const int m = std::min(512, static_cast<int>(x.size()) - off);
            float* ptr = x.data() + off;
            p.process(&ptr, 1, m);
        }
    } else {
        mastering::dsp::HardClipper p;
        mastering::dsp::HardClipSettings s;
        s.ceilingDb = -6.0;
        s.driveDb = 12.0;
        s.oversamplingFactor = factor;
        p.prepare(sr, 2048, 1, factor);
        p.setSettings(s);
        for (int off = 0; off < static_cast<int>(x.size()); off += 512) {
            const int m = std::min(512, static_cast<int>(x.size()) - off);
            float* ptr = x.data() + off;
            p.process(&ptr, 1, m);
        }
    }
}

void copyTree(const fs::path& src, const fs::path& dst)
{
    fs::create_directories(dst);
    for (auto& e : fs::recursive_directory_iterator(src)) {
        const auto rel = fs::relative(e.path(), src);
        const auto target = dst / rel;
        if (e.is_directory())
            fs::create_directories(target);
        else if (e.is_regular_file()) {
            fs::create_directories(target.parent_path());
            fs::copy_file(e.path(), target, fs::copy_options::overwrite_existing);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    fs::path outDir = "artifacts/dsp_validation";
    if (argc > 1)
        outDir = argv[1];
    fs::create_directories(outDir);
    fs::create_directories(outDir / "loudness_matched");

    constexpr double sr = 48'000.0;
    auto dry = makeDry(sr, 2.0);
    writeWav(outDir / "dry.wav", dry, sr);

    std::ostringstream aliasMd;
    aliasMd << "# Aliasing report\n\n"
            << "Stimulus: 9 kHz sine @ 48 kHz (inside OS passband ≈ 0.45·Nyquist).\n"
            << "Metric: DFT spurious/total energy excluding fundamental + odd harmonics below Nyquist.\n"
            << "Lower is better. Oversampled modes must beat 1×.\n\n"
            << "| Processor | Factor | Aliasing metric | Delta vs 1× | Result |\n"
            << "|---|---:|---:|---:|---|\n";

    const auto runAliasRow = [&](const char* proc, const char* kind) {
        double base = 0.0;
        for (int factor : {1, 2, 4, 8}) {
            auto x = makeNearNyquist(sr, 1.0, 9'000.0);
            processMonoNonlinear(x, factor, sr, kind);
            if (x.size() > 4096)
                x.erase(x.begin(), x.begin() + 2048);
            const auto metric = aliasingEnergyRatio(x, sr, 9'000.0);
            if (factor == 1)
                base = metric;
            const auto delta = metric - base;
            const bool pass = factor == 1 || metric < base;
            aliasMd << "| " << proc << " | " << factor << " | " << metric << " | " << delta
                    << " | " << (factor == 1 ? "baseline" : (pass ? "PASS" : "CHECK")) << " |\n";
        }
    };
    runAliasRow("saturation", "sat");
    runAliasRow("soft_clip", "soft");
    runAliasRow("hard_clip", "hard");

    auto runAndWrite = [&](mastering::dsp::MasterSafetySettings s,
                            int factor,
                            const fs::path& name,
                            bool matchLoudness) {
        mastering::dsp::MasterSafetyMeters meters {};
        auto wet = processChain(dry, s, factor, sr, &meters);
        writeWav(outDir / name, wet, sr);
        if (matchLoudness) {
            auto matched = loudnessMatch(wet, dry);
            writeWav(outDir / "loudness_matched" / name, matched, sr);
        }
        const auto qc = mastering::dsp::ExportQc::analyse(
            wet, sr, 32, meters, 0, s.limiterEnabled ? s.limiter.ceilingDbTp : 0.0, 0.15);
        std::ofstream(outDir / (name.stem().string() + ".qc.json")) << mastering::dsp::ExportQc::toJson(qc);
        return wet;
    };

    {
        mastering::dsp::MasterSafetySettings s;
        s.saturationEnabled = true;
        s.saturation.drive = 4.0;
        s.limiterEnabled = false;
        s.clipMode = mastering::dsp::ClipMode::none;
        runAndWrite(s, 1, "saturation_1x.wav", true);
        runAndWrite(s, 4, "saturation_4x.wav", true);
    }
    {
        mastering::dsp::MasterSafetySettings s;
        s.clipMode = mastering::dsp::ClipMode::soft;
        s.softClip.thresholdDb = -1.0;
        s.softClip.driveDb = 6.0;
        s.limiterEnabled = false;
        runAndWrite(s, 1, "soft_clip_1x.wav", true);
        runAndWrite(s, 4, "soft_clip_4x.wav", true);
    }
    {
        mastering::dsp::MasterSafetySettings s;
        s.clipMode = mastering::dsp::ClipMode::hard;
        s.hardClip.ceilingDb = -1.0;
        s.hardClip.driveDb = 6.0;
        s.limiterEnabled = false;
        runAndWrite(s, 1, "hard_clip_1x.wav", true);
        runAndWrite(s, 4, "hard_clip_4x.wav", true);
    }

    std::ostringstream limMd;
    limMd << "# Limiter report\n\n"
          << "Tolerance: +0.15 dB above configured ceiling (declared before run).\n\n"
          << "| Signal | Ceiling | Output TP | Delta | Max GR | Result |\n"
          << "|---|---:|---:|---:|---:|---|\n";

    for (double ceiling : {-0.1, -1.0}) {
        mastering::dsp::MasterSafetySettings s;
        s.limiterEnabled = true;
        s.limiter.ceilingDbTp = ceiling;
        s.limiter.lookAheadMs = 2.0;
        s.limiter.oversamplingFactor = 4;
        s.clipMode = mastering::dsp::ClipMode::none;
        s.saturationEnabled = false;

        auto hot = dry;
        for (auto& c : hot)
            for (auto& v : c)
                v *= 2.0f;

        mastering::dsp::MasterSafetyMeters meters {};
        auto out = processChain(hot, s, 4, sr, &meters);
        const char* name = ceiling > -0.5 ? "limiter_minus_0_1.wav" : "limiter_minus_1_0.wav";
        writeWav(outDir / name, out, sr);

        const auto tp = truePeakDb(out, sr);
        const auto ok = tp <= ceiling + 0.15;
        limMd << "| dense | " << ceiling << " | " << tp << " | " << (tp - ceiling) << " | "
              << meters.limiterMaxGrDb << " | " << (ok ? "PASS" : "FAIL") << " |\n";

        const auto qc = mastering::dsp::ExportQc::analyse(out, sr, 32, meters, 0, ceiling, 0.15);
        std::ofstream(outDir / (std::string(name) + ".qc.json")) << mastering::dsp::ExportQc::toJson(qc);
    }

    std::ostringstream latMd;
    latMd << "# Latency report\n\n"
          << "| Processor | Reported | Measured | Delta | Result |\n"
          << "|---|---:|---:|---:|---|\n";
    for (int factor : {1, 2, 4, 8}) {
        mastering::dsp::Oversampler os;
        os.prepare(sr, 2048, 1, factor);
        std::vector<float> x(2048, 0.0f);
        x[0] = 1.0f;
        float* p = x.data();
        os.process(&p, 1, 2048, [](float* const*, int, int, double) {});
        int meas = 0;
        double peak = 0.0;
        for (int i = 0; i < 2048; ++i) {
            const auto a = std::abs(x[static_cast<std::size_t>(i)]);
            if (a > peak) {
                peak = a;
                meas = i;
            }
        }
        const auto rep = os.latencySamplesBaseRate();
        latMd << "| oversampler_" << factor << "x | " << rep << " | " << meas << " | "
              << (meas - rep) << " | " << (std::abs(meas - rep) <= 2 ? "PASS" : "FAIL") << " |\n";
    }
    {
        mastering::dsp::TruePeakLimiter lim;
        mastering::dsp::TruePeakLimiterSettings s;
        s.lookAheadMs = 2.0;
        s.oversamplingFactor = 4;
        lim.prepare(sr, 2048, 1, 4);
        lim.setSettings(s);
        std::vector<float> x(4096, 0.0f);
        x[0] = 1.0f;
        float* p = x.data();
        lim.process(&p, 1, 2048);
        float* p2 = x.data() + 2048;
        lim.process(&p2, 1, 2048);
        int meas = 0;
        double peak = 0.0;
        for (int i = 0; i < 4096; ++i) {
            const auto a = std::abs(x[static_cast<std::size_t>(i)]);
            if (a > peak) {
                peak = a;
                meas = i;
            }
        }
        const auto rep = lim.latencySamples();
        latMd << "| true_peak_limiter_4x | " << rep << " | " << meas << " | " << (meas - rep)
              << " | " << (std::abs(meas - rep) <= 2 ? "PASS" : "FAIL") << " |\n";
    }

    // Aggregate QC on limiter −1.0 render (master-safety export path).
    {
        mastering::dsp::MasterSafetySettings s;
        s.limiterEnabled = true;
        s.limiter.ceilingDbTp = -1.0;
        s.limiter.lookAheadMs = 2.0;
        s.limiter.oversamplingFactor = 4;
        mastering::dsp::MasterSafetyMeters meters {};
        auto hot = dry;
        for (auto& c : hot)
            for (auto& v : c)
                v *= 2.0f;
        auto limited = processChain(hot, s, 4, sr, &meters);
        const auto qc = mastering::dsp::ExportQc::analyse(limited, sr, 32, meters, 0, -1.0, 0.15);
        std::ofstream(outDir / "qc_report.json") << mastering::dsp::ExportQc::toJson(qc);
        std::ofstream(outDir / "qc_report.md") << mastering::dsp::ExportQc::toMarkdown(qc);
    }
    std::ofstream(outDir / "aliasing_report.md") << aliasMd.str();
    std::ofstream(outDir / "latency_report.md") << latMd.str();
    std::ofstream(outDir / "limiter_report.md") << limMd.str();

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
        << "8. Compare 1× vs 4× of the same processor\n\n"
        << "Do not call a processor musical or transparent solely because unit tests pass.\n";

    // Benchmark smoke (not a pass/fail gate).
    std::ostringstream bench;
    bench << "# DSP benchmark smoke\n\n"
          << "| Processor | SR | Block | OS | Channels | CPU/time (s) | Allocations | Result |\n"
          << "|---|---:|---:|---:|---:|---:|---:|---|\n";
    for (int block : {64, 256, 1024}) {
        for (int factor : {1, 4}) {
            mastering::dsp::MasterSafetySettings s;
            s.saturationEnabled = true;
            s.saturation.drive = 3.0;
            s.clipMode = mastering::dsp::ClipMode::soft;
            s.limiterEnabled = true;
            s.limiter.ceilingDbTp = -1.0;
            mastering::dsp::MasterSafetyChain chain;
            chain.prepare(sr, block, 2, factor);
            chain.setSettings(s);
            std::vector<float> l(static_cast<std::size_t>(block), 0.1f);
            std::vector<float> r(static_cast<std::size_t>(block), 0.1f);
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 2000; ++i) {
                float* ch[2] {l.data(), r.data()};
                chain.process(ch, 2, block);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double sec =
                std::chrono::duration<double>(t1 - t0).count();
            const double audioSec = 2000.0 * block / sr;
            bench << "| master_safety_chain | " << sr << " | " << block << " | " << factor
                  << " | 2 | " << sec << " (RTF " << (audioSec / sec) << ") | n/a | smoke |\n";
        }
    }
    std::ofstream(outDir / "benchmark_smoke.md") << bench.str();

    if (fs::exists("/opt/cursor/artifacts")) {
        copyTree(outDir, "/opt/cursor/artifacts/dsp_validation");
    }

    std::cout << "Wrote DSP validation artifacts to " << outDir << "\n";
    return 0;
}
