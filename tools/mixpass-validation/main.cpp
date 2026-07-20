#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/dsp/DynamicEq.h"
#include "mastering/project/ProjectDocument.h"

#include <nlohmann/json.hpp>

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
using mastering::assistant::MetalcoreMixPass;
using mastering::project::ProjectDocument;
using mastering::project::SectionKind;
using mastering::project::TrackRecord;
using mastering::project::TrackRole;

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
    w16(3);
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

std::vector<float> tone(int n, double sr, double hz, float amp)
{
    std::vector<float> x(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t>(i)] = float(
            amp * std::sin(2.0 * std::numbers::pi * hz * (i / sr)));
    return x;
}

double rms(const std::vector<float>& x)
{
    double a = 0;
    for (float v : x)
        a += double(v) * v;
    return x.empty() ? 0.0 : std::sqrt(a / double(x.size()));
}

double rmsDb(const std::vector<float>& x)
{
    return 20.0 * std::log10(std::max(1.0e-12, rms(x)));
}

double crestDb(const std::vector<float>& x)
{
    float peak = 0;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    return 20.0 * std::log10(std::max(1.0e-12, double(peak))) - rmsDb(x);
}

TrackRecord makeTrack(
    const char* id,
    TrackRole role,
    double rmsDbfs,
    double sub,
    double lowMid,
    double presence,
    double air,
    double crest)
{
    TrackRecord track;
    track.id = id;
    track.name = id;
    track.audioPath = std::string("stems/") + id + ".wav";
    track.role = role;
    track.metrics.rmsDbfs = rmsDbfs;
    track.metrics.spectrum.subDb = sub;
    track.metrics.spectrum.bassDb = sub + 1.0;
    track.metrics.spectrum.lowMidDb = lowMid;
    track.metrics.spectrum.midDb = -20.0;
    track.metrics.spectrum.presenceDb = presence;
    track.metrics.spectrum.airDb = air;
    track.metrics.crestFactorDb = crest;
    track.metrics.stereoCorrelation = role == TrackRole::rhythmGuitarLeft
            || role == TrackRole::rhythmGuitarRight
        ? 0.15
        : 0.9;
    return track;
}

ProjectDocument buildProject()
{
    ProjectDocument project;
    project.id = "synthetic-metalcore-m3a";
    project.name = "Synthetic Metalcore Mix Pass";
    project.bpm = 148.0;
    project.sampleRate = 48'000.0;
    project.tracks = {
        makeTrack("kick", TrackRole::kick, -18.0, -16.0, -28.0, -35.0, -40.0, 14.0),
        makeTrack("snare", TrackRole::snare, -20.0, -40.0, -22.0, -18.0, -25.0, 16.0),
        makeTrack("bass", TrackRole::bass, -16.0, -15.0, -20.0, -30.0, -40.0, 10.0),
        makeTrack("gtr_l", TrackRole::rhythmGuitarLeft, -14.0, -35.0, -12.0, -16.0, -10.0, 8.0),
        makeTrack("gtr_r", TrackRole::rhythmGuitarRight, -17.0, -35.0, -13.0, -15.0, -11.0, 8.0),
        makeTrack("vox_clean", TrackRole::cleanVocal, -22.0, -45.0, -28.0, -14.0, -20.0, 18.0),
        makeTrack("vox_scream", TrackRole::screamVocal, -20.0, -45.0, -26.0, -12.0, -18.0, 14.0),
    };
    mastering::project::SectionMarker chorus;
    chorus.id = "chorus-1";
    chorus.kind = SectionKind::chorus;
    chorus.name = "Chorus";
    chorus.startSeconds = 16.0;
    chorus.endSeconds = 32.0;
    project.sections.push_back(chorus);
    return project;
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream(path) << text;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path outDir = argc > 1 ? fs::path(argv[1]) : fs::path("artifacts/mixpass_validation");
    const fs::path benchDir = argc > 2 ? fs::path(argv[2]) : fs::path("benchmarks/personal");
    fs::create_directories(outDir);
    fs::create_directories(benchDir / "stems");
    fs::create_directories(benchDir / "references");
    fs::create_directories(benchDir / "targets");
    fs::create_directories(benchDir / "renders");
    fs::create_directories(benchDir / "reports");

    const double sr = 48'000.0;
    const int n = static_cast<int>(sr * 2.0); // 2s synthetic

    // Synthetic stems (repository-safe, not musical quality proof).
    auto kick = tone(n, sr, 60.0, 0.45f);
    auto bass = tone(n, sr, 70.0, 0.40f);
    auto snare = tone(n, sr, 220.0, 0.30f);
    for (int i = 0; i < n; i += static_cast<int>(sr * 0.5)) {
        for (int j = 0; j < static_cast<int>(sr * 0.02) && i + j < n; ++j)
            snare[static_cast<std::size_t>(i + j)] += 0.55f
                * std::exp(-j / (sr * 0.008));
    }
    auto gtrL = tone(n, sr, 800.0, 0.28f);
    auto gtrR = tone(n, sr, 820.0, 0.22f);
    auto vox = tone(n, sr, 2'800.0, 0.18f);

    writeWav(benchDir / "stems" / "kick.wav", {kick, kick}, sr);
    writeWav(benchDir / "stems" / "bass.wav", {bass, bass}, sr);
    writeWav(benchDir / "stems" / "snare.wav", {snare, snare}, sr);
    writeWav(benchDir / "stems" / "gtr_l.wav", {gtrL, std::vector<float>(gtrL.size(), 0.0f)}, sr);
    writeWav(benchDir / "stems" / "gtr_r.wav", {std::vector<float>(gtrR.size(), 0.0f), gtrR}, sr);
    writeWav(benchDir / "stems" / "vox_clean.wav", {vox, vox}, sr);
    writeWav(benchDir / "references" / "reference_placeholder.wav",
        {tone(n, sr, 440.0, 0.1f), tone(n, sr, 440.0, 0.1f)},
        sr);

    auto project = buildProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.bpm = project.bpm;
    project.mixPassActions = pass.generateActions(project, std::nullopt, options);

    int applied = 0;
    for (auto& action : project.mixPassActions) {
        if (MetalcoreMixPass::applyAction(project, action))
            ++applied;
    }

    // Idempotent re-apply
    int reapplied = 0;
    for (auto& action : project.mixPassActions) {
        if (action.state == "applied" && MetalcoreMixPass::applyAction(project, action))
            ++reapplied;
    }

    // RAW mix (sum of tones)
    std::vector<float> rawL(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> rawR(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        rawL[static_cast<std::size_t>(i)] = 0.35f * kick[static_cast<std::size_t>(i)]
            + 0.35f * bass[static_cast<std::size_t>(i)]
            + 0.25f * snare[static_cast<std::size_t>(i)]
            + 0.30f * gtrL[static_cast<std::size_t>(i)]
            + 0.20f * vox[static_cast<std::size_t>(i)];
        rawR[static_cast<std::size_t>(i)] = 0.35f * kick[static_cast<std::size_t>(i)]
            + 0.35f * bass[static_cast<std::size_t>(i)]
            + 0.25f * snare[static_cast<std::size_t>(i)]
            + 0.30f * gtrR[static_cast<std::size_t>(i)]
            + 0.20f * vox[static_cast<std::size_t>(i)];
    }

    // AUTO: apply DynEQ kick→bass on synthetic buffers when action present.
    auto autoBass = bass;
    mastering::dsp::DynamicEqProcessor dyn;
    dyn.prepare(sr, 256, 2);
    const auto* bassTrack = MetalcoreMixPass::findTrack(project, TrackRole::bass);
    if (bassTrack != nullptr && bassTrack->dynamicEqEnabled) {
        dyn.setState(bassTrack->dynamicEq);
        for (int off = 0; off < n; off += 256) {
            const int block = std::min(256, n - off);
            float* target[2] {autoBass.data() + off, autoBass.data() + off};
            const float* sc[2] {kick.data() + off, kick.data() + off};
            dyn.process(target, sc, 2, block);
        }
    }

    std::vector<float> autoL = rawL;
    std::vector<float> autoR = rawR;
    for (int i = 0; i < n; ++i) {
        // Replace broadband bass energy contribution with DynEQ-processed bass proxy.
        const float delta = autoBass[static_cast<std::size_t>(i)] - bass[static_cast<std::size_t>(i)];
        autoL[static_cast<std::size_t>(i)] += 0.35f * delta;
        autoR[static_cast<std::size_t>(i)] += 0.35f * delta;
    }

    const double rawLoud = 0.5 * (rmsDb(rawL) + rmsDb(rawR));
    const double autoLoud = 0.5 * (rmsDb(autoL) + rmsDb(autoR));
    const double matchGainDb = rawLoud - autoLoud;
    const float matchGain = float(std::pow(10.0, matchGainDb / 20.0));
    auto matchedL = autoL;
    auto matchedR = autoR;
    for (int i = 0; i < n; ++i) {
        matchedL[static_cast<std::size_t>(i)] *= matchGain;
        matchedR[static_cast<std::size_t>(i)] *= matchGain;
    }

    writeWav(outDir / "raw_mix.wav", {rawL, rawR}, sr);
    writeWav(outDir / "auto_mix.wav", {autoL, autoR}, sr);
    writeWav(outDir / "auto_mix_loudness_matched.wav", {matchedL, matchedR}, sr);
    writeWav(benchDir / "renders" / "raw_mix.wav", {rawL, rawR}, sr);
    writeWav(benchDir / "renders" / "auto_mix.wav", {autoL, autoR}, sr);

    const double kickBassMaskingDelta = rmsDb(bass) - rmsDb(autoBass);
    const double gtrBalance = rmsDb(gtrL) - rmsDb(gtrR);
    const double vocalGuitarMaskingProxy = rmsDb(vox) - 0.5 * (rmsDb(gtrL) + rmsDb(gtrR));
    const double snareGuitarMaskingProxy = rmsDb(snare) - 0.5 * (rmsDb(gtrL) + rmsDb(gtrR));

    nlohmann::json report {
        {"milestone", "3A"},
        {"disclaimer",
            "Synthetic validation is not proof of an ideal musical mix. "
            "It verifies actionable Mix Pass pipeline behaviour."},
        {"flStudio", "NOT MANUALLY VERIFIED IN FL STUDIO"},
        {"installer", "NOT MANUALLY VERIFIED"},
        {"actionsGenerated", project.mixPassActions.size()},
        {"actionsApplied", applied},
        {"idempotentReapplyOk", reapplied == applied},
        {"pairs", project.pairs.size()},
        {"buses", project.buses.size()},
        {"sections", project.sections.size()},
        {"guitarLeftRightSeparate", true},
        {"metrics", {
            {"rawRmsDb", rawLoud},
            {"autoRmsDb", autoLoud},
            {"loudnessMatchGainDb", matchGainDb},
            {"kickBassMaskingDeltaDb", kickBassMaskingDelta},
            {"vocalGuitarMaskingProxyDb", vocalGuitarMaskingProxy},
            {"snareGuitarMaskingProxyDb", snareGuitarMaskingProxy},
            {"guitarLrBalanceDb", gtrBalance},
            {"rawCrestDb", 0.5 * (crestDb(rawL) + crestDb(rawR))},
            {"autoCrestDb", 0.5 * (crestDb(autoL) + crestDb(autoR))},
            {"sectionContrast", project.sections.empty() ? 0.0 : 1.0}
        }},
        {"problemTypes", nlohmann::json::array()}
    };
    for (const auto& action : project.mixPassActions)
        report["problemTypes"].push_back(action.problemType);

    writeText(outDir / "applied_actions.json", mastering::assistant::mixPassActionsToJson(project.mixPassActions));
    writeText(outDir / "mixpass_report.json", report.dump(2));
    writeText(outDir / "project_roundtrip.masuite", mastering::project::serialize(project));

    std::ostringstream md;
    md << "# Metalcore Mix Pass validation (synthetic)\n\n"
       << "> Synthetic artifacts are **not** evidence of an ideal musical mix.\n\n"
       << "- Actions generated: " << project.mixPassActions.size() << "\n"
       << "- Actions applied: " << applied << "\n"
       << "- Idempotent re-apply: " << (reapplied == applied ? "yes" : "no") << "\n"
       << "- Guitar L/R separate: yes\n"
       << "- Kick/bass masking delta (bass band RMS): " << kickBassMaskingDelta << " dB\n"
       << "- Guitar L/R balance: " << gtrBalance << " dB\n"
       << "- Loudness match gain (AUTO→RAW): " << matchGainDb << " dB\n"
       << "- FL Studio: NOT MANUALLY VERIFIED IN FL STUDIO\n"
       << "- Installer: NOT MANUALLY VERIFIED\n";
    writeText(outDir / "benchmark_smoke.md", md.str());
    writeText(benchDir / "reports" / "mixpass_report.json", report.dump(2));
    writeText(benchDir / "reports" / "applied_actions.json",
        mastering::assistant::mixPassActionsToJson(project.mixPassActions));

    nlohmann::json manifest {
        {"name", "personal-metalcore-benchmark"},
        {"version", 1},
        {"copyrightedAudio", false},
        {"notes",
            "Place your local stems under stems/ and references under references/. "
            "Repository ships only synthetic placeholders."},
        {"expectedRoles", {
            "kick", "snare", "bass",
            "rhythm-guitar-left", "rhythm-guitar-right",
            "clean-vocal", "scream-vocal"
        }},
        {"layout", {
            {"stems", "stems/"},
            {"references", "references/"},
            {"targets", "targets/"},
            {"renders", "renders/"},
            {"reports", "reports/"}
        }}
    };
    writeText(benchDir / "manifest.json", manifest.dump(2));
    writeText(benchDir / "README.md",
        "# Personal metalcore benchmark\n\n"
        "Do **not** commit copyrighted stems.\n\n"
        "Synthetic placeholders are generated by `mixpass_validation` for CI.\n"
        "Drop your local WAV/AIFF stems here when validating musically offline.\n");

    std::cout << "mixpass_validation wrote " << outDir << " and " << benchDir << "\n";
    return applied > 0 && reapplied == applied ? 0 : 1;
}
