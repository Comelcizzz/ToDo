#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/analysis/LoudnessMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <vector>

namespace {

std::vector<std::vector<float>> makeStereoSine(
    double sampleRate,
    double seconds,
    double frequencyHz,
    double peakAmplitude)
{
    const auto frames = static_cast<std::size_t>(std::lround(sampleRate * seconds));
    std::vector<std::vector<float>> channels(2, std::vector<float>(frames));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(
            peakAmplitude
            * std::sin(2.0 * std::numbers::pi * frequencyHz * static_cast<double>(frame) / sampleRate));
        channels[0][frame] = value;
        channels[1][frame] = value;
    }
    return channels;
}

void feed(
    mastering::analysis::LoudnessMeter& meter,
    const std::vector<std::vector<float>>& channels,
    int blockSize)
{
    const auto frames = channels.front().size();
    for (std::size_t start = 0; start < frames;) {
        const auto count = static_cast<int>(
            std::min(frames - start, static_cast<std::size_t>(std::max(1, blockSize))));
        std::vector<const float*> pointers(channels.size());
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            pointers[channel] = channels[channel].data() + start;
        meter.process(pointers.data(), static_cast<int>(channels.size()), count);
        start += static_cast<std::size_t>(count);
    }
}

struct Row {
    std::string metric;
    double sampleRate {0};
    int blockSize {0};
    double offline {0};
    double realtime {0};
    double delta {0};
    double tolerance {0};
    bool pass {false};
};

} // namespace

TEST_CASE("Metering invariance matrix writes validation artifacts", "[milestone1a][matrix]")
{
    const auto amplitude = std::pow(10.0, -23.0 / 20.0);
    const std::vector<double> rates {44'100.0, 48'000.0, 96'000.0};
    const std::vector<int> blocks {32, 64, 128, 256, 512, 1024};
    std::vector<Row> rows;

    for (double sampleRate : rates) {
        const auto channels = makeStereoSine(sampleRate, 4.0, 1'000.0, amplitude);
        mastering::analysis::LoudnessMeter offline;
        offline.prepare(sampleRate, 2, 4096);
        feed(offline, channels, 256);
        offline.finalize();
        const auto offlineReading = offline.snapshot();
        REQUIRE(offlineReading.integratedValid);

        for (int block : blocks) {
            mastering::analysis::LoudnessMeter realtime;
            realtime.prepare(sampleRate, 2, 4096);
            feed(realtime, channels, block);
            realtime.finalize();
            const auto rt = realtime.snapshot();

            Row integrated {
                "integratedLufs",
                sampleRate,
                block,
                offlineReading.integratedLufs,
                rt.integratedLufs,
                std::abs(rt.integratedLufs - offlineReading.integratedLufs),
                0.05,
                false
            };
            integrated.pass = integrated.delta <= integrated.tolerance;
            rows.push_back(integrated);

            if (offlineReading.shortTermValid && rt.shortTermValid) {
                Row st {
                    "shortTermLufs",
                    sampleRate,
                    block,
                    offlineReading.shortTermLufs,
                    rt.shortTermLufs,
                    std::abs(rt.shortTermLufs - offlineReading.shortTermLufs),
                    0.15,
                    false
                };
                st.pass = st.delta <= st.tolerance;
                rows.push_back(st);
            }

            Row tp {
                "truePeakLinear",
                sampleRate,
                block,
                offlineReading.truePeakLinear,
                rt.truePeakLinear,
                std::abs(rt.truePeakLinear - offlineReading.truePeakLinear),
                1.0e-5,
                false
            };
            tp.pass = tp.delta <= tp.tolerance;
            rows.push_back(tp);
        }
    }

    const auto jsonPath = "metering-validation.json";
    const auto mdPath = "metering-validation.md";
    {
        std::ofstream json(jsonPath);
        json << "{\n  \"rows\": [\n";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            json << "    {\"metric\":\"" << row.metric << "\",\"sampleRate\":" << row.sampleRate
                 << ",\"blockSize\":" << row.blockSize << ",\"offline\":" << row.offline
                 << ",\"realtime\":" << row.realtime << ",\"delta\":" << row.delta
                 << ",\"tolerance\":" << row.tolerance
                 << ",\"result\":\"" << (row.pass ? "PASS" : "FAIL") << "\"}";
            json << (i + 1 < rows.size() ? ",\n" : "\n");
        }
        json << "  ]\n}\n";
    }
    {
        std::ofstream md(mdPath);
        md << "| Metric | SR | Block size | Offline | Realtime | Delta | Tolerance | Result |\n";
        md << "|---|---:|---:|---:|---:|---:|---:|---|\n";
        for (const auto& row : rows) {
            md << "| " << row.metric << " | " << row.sampleRate << " | " << row.blockSize << " | "
               << row.offline << " | " << row.realtime << " | " << row.delta << " | "
               << row.tolerance << " | " << (row.pass ? "PASS" : "FAIL") << " |\n";
        }
    }

    // Also copy under /opt/cursor/artifacts when available.
    std::ifstream inJson(jsonPath);
    if (inJson) {
        std::ofstream outJson("/opt/cursor/artifacts/metering-validation.json");
        outJson << inJson.rdbuf();
    }
    std::ifstream inMd(mdPath);
    if (inMd) {
        std::ofstream outMd("/opt/cursor/artifacts/metering-validation.md");
        outMd << inMd.rdbuf();
    }

    for (const auto& row : rows)
        CHECK(row.pass);
}

TEST_CASE("Integrated offline vs streamed blocks with silence pockets", "[milestone1a][integrated]")
{
    constexpr double sampleRate = 48'000.0;
    const auto loudAmp = std::pow(10.0, -20.0 / 20.0);
    const auto quietAmp = std::pow(10.0, -40.0 / 20.0);
    const auto total = static_cast<std::size_t>(sampleRate * 8.0);
    std::vector<std::vector<float>> channels(2, std::vector<float>(total, 0.0f));
    for (std::size_t i = 0; i < total; ++i) {
        double amp = 0.0;
        if (i > static_cast<std::size_t>(sampleRate) && i < static_cast<std::size_t>(sampleRate * 2))
            amp = quietAmp;
        else if (i >= static_cast<std::size_t>(sampleRate * 2) && i < static_cast<std::size_t>(sampleRate * 5))
            amp = loudAmp;
        const auto value = static_cast<float>(
            amp * std::sin(2.0 * std::numbers::pi * 1'000.0 * static_cast<double>(i) / sampleRate));
        channels[0][i] = value;
        channels[1][i] = value;
    }

    mastering::analysis::LoudnessMeter oneShot;
    oneShot.prepare(sampleRate, 2, 4096);
    feed(oneShot, channels, 512);
    oneShot.finalize();

    mastering::analysis::LoudnessMeter streamed;
    streamed.prepare(sampleRate, 2, 4096);
    feed(streamed, channels, 97);
    streamed.finalize();

    REQUIRE(oneShot.snapshot().integratedValid);
    REQUIRE(streamed.snapshot().integratedValid);
    CHECK(streamed.snapshot().integratedLufs
        == Catch::Approx(oneShot.snapshot().integratedLufs).margin(0.05));

    streamed.reset();
    CHECK(streamed.snapshot().integratedState == mastering::analysis::MetricAvailability::stale);
}

TEST_CASE("Synthetic edge signals do not crash metering", "[milestone1a][synthetic]")
{
    mastering::analysis::LoudnessMeter meter;
    meter.prepare(48'000.0, 2, 1024);

    auto run = [&](auto fill) {
        meter.reset();
        std::vector<std::vector<float>> channels(2, std::vector<float>(4096));
        fill(channels);
        feed(meter, channels, 128);
        meter.finalize();
        CHECK(std::isfinite(meter.snapshot().samplePeakLinear));
    };

    run([](auto& c) {}); // silence
    run([](auto& c) { c[0][0] = 1.0f; }); // impulse
    run([](auto& c) {
        for (auto& s : c[0])
            s = 0.2f;
        for (auto& s : c[1])
            s = 0.2f;
    }); // DC
    run([](auto& c) {
        for (std::size_t i = 0; i < c[0].size(); ++i) {
            c[0][i] = 0.3f;
            c[1][i] = -0.3f;
        }
    }); // anti-phase
    run([](auto& c) {
        for (auto& s : c[0])
            s = 0.4f;
    }); // left-only
    run([](auto& c) {
        for (auto& s : c[1])
            s = 0.4f;
    }); // right-only
    run([](auto& c) {
        for (std::size_t i = 0; i < c[0].size(); ++i) {
            c[0][i] = (i == 10) ? std::numeric_limits<float>::quiet_NaN() : 0.0f;
            c[1][i] = (i == 11) ? std::numeric_limits<float>::infinity() : 0.0f;
        }
    });
}
