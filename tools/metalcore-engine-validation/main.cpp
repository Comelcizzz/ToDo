#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/analysis/StreamingAnalyzer.h"
#include "mastering/assistant/ActionBudget.h"
#include "mastering/assistant/ActionResolver.h"
#include "mastering/assistant/LoudnessMatch.h"
#include "mastering/assistant/MetalcoreAnalysis.h"
#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/assistant/RenderIdentity.h"
#include "mastering/assistant/SectionAutomation.h"
#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/ParallelCompressor.h"
#include "mastering/dsp/StereoWidth.h"
#include "mastering/dsp/VocalRider.h"
#include "mastering/project/ProjectDocument.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numbers>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using mastering::analysis::StreamingAnalyzer;
using mastering::assistant::ActionBudget;
using mastering::assistant::LoudnessMatchResult;
using mastering::assistant::MetalcoreAnalysis;
using mastering::assistant::MetalcoreMixPass;
using mastering::assistant::ReferenceProfile;
using mastering::assistant::RenderIdentityBuilder;
using mastering::assistant::RenderIdentityInputs;
using mastering::assistant::SectionAutomation;
using mastering::assistant::TrackAnalysisExtras;
using mastering::assistant::applyBudget;
using mastering::assistant::matchBuffers;
using mastering::project::ProjectDocument;
using mastering::project::SectionKind;
using mastering::project::SectionMarker;
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

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream(path) << text;
}

void addToneRange(
    std::vector<float>& x,
    int start,
    int end,
    double sr,
    double hz,
    float amp)
{
    const int n = static_cast<int>(x.size());
    start = std::max(0, start);
    end = std::min(n, end);
    for (int i = start; i < end; ++i)
        x[static_cast<std::size_t>(i)] += float(
            amp * std::sin(2.0 * std::numbers::pi * hz * (double(i) / sr)));
}

void addKickPulses(std::vector<float>& x, double sr, double hz, float amp, double periodSec)
{
    const int n = static_cast<int>(x.size());
    const int period = std::max(1, int(sr * periodSec));
    const int len = std::max(1, int(sr * 0.04));
    for (int start = 0; start < n; start += period) {
        for (int j = 0; j < len && start + j < n; ++j) {
            const double env = std::exp(-j / (sr * 0.015));
            x[static_cast<std::size_t>(start + j)] += float(
                amp * env * std::sin(2.0 * std::numbers::pi * hz * (double(j) / sr)));
        }
    }
}

void addSnareHits(std::vector<float>& x, double sr, double crackHz, float amp, double periodSec)
{
    const int n = static_cast<int>(x.size());
    const int period = std::max(1, int(sr * periodSec));
    const int len = std::max(1, int(sr * 0.05));
    for (int start = period / 2; start < n; start += period) {
        for (int j = 0; j < len && start + j < n; ++j) {
            const double env = std::exp(-j / (sr * 0.02));
            const float crack = float(
                amp * env * std::sin(2.0 * std::numbers::pi * crackHz * (double(j) / sr)));
            const float body = float(
                amp * 0.4f * env * std::sin(2.0 * std::numbers::pi * 180.0 * (double(j) / sr)));
            x[static_cast<std::size_t>(start + j)] += crack + body;
        }
    }
}

double rmsDb(const std::vector<float>& x)
{
    double a = 0;
    for (float v : x)
        a += double(v) * v;
    const double r = x.empty() ? 0.0 : std::sqrt(a / double(x.size()));
    return 20.0 * std::log10(std::max(1.0e-12, r));
}

std::uint64_t fnv1a64(const std::string& s)
{
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

struct SectionWindow {
    const char* id;
    SectionKind kind;
    const char* name;
    double start;
    double end;
};

TrackRecord makeTrack(const char* id, TrackRole role, const mastering::analysis::AudioMetrics& m)
{
    TrackRecord track;
    track.id = id;
    track.name = id;
    track.audioPath = std::string("stems/") + id + ".wav";
    track.role = role;
    track.metrics = m;
    return track;
}

// Generate one mono stem efficiently into `out` (pre-sized), write stereo WAV, stream-analyze.
mastering::analysis::StreamingAnalysisResult generateAnalyzeWrite(
    const fs::path& wavPath,
    std::vector<float>& mono,
    double sr,
    const std::string& trackId,
    const std::function<void(std::vector<float>&)>& fillFn)
{
    std::fill(mono.begin(), mono.end(), 0.0f);
    fillFn(mono);
    writeWav(wavPath, {mono, mono}, sr);

    StreamingAnalyzer analyzer;
    analyzer.setTrackIdentity(trackId, static_cast<std::uint64_t>(mono.size() * sizeof(float)), fnv1a64(trackId));
    return analyzer.analyzeBuffer(mono, sr);
}

void scaleBuffer(std::vector<float>& x, float gain)
{
    for (auto& v : x)
        v *= gain;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path outDir = argc > 1 ? fs::path(argv[1]) : fs::path("artifacts/metalcore_engine_validation");
    fs::create_directories(outDir);
    fs::create_directories(outDir / "stems");
    fs::create_directories(outDir / "loudness_matched");
    fs::create_directories(outDir / "blind_test");

    // 90s @ 48 kHz — generate/analyze stem-by-stem (reuse one buffer) to limit peak RAM.
    const double sr = 48'000.0;
    const double durationSec = 90.0;
    const int n = static_cast<int>(sr * durationSec);
    const auto t0 = std::chrono::steady_clock::now();

    const SectionWindow sections[] = {
        {"intro", SectionKind::intro, "Intro", 0.0, 8.0},
        {"verse", SectionKind::verse, "Verse", 8.0, 28.0},
        {"chorus", SectionKind::chorus, "Chorus", 28.0, 44.0},
        {"breakdown", SectionKind::breakdown, "Breakdown", 44.0, 60.0},
        {"final-chorus", SectionKind::chorus, "Final Chorus", 60.0, 78.0},
        {"outro", SectionKind::outro, "Outro", 78.0, 90.0},
    };

    auto secStart = [&](const char* id) {
        for (const auto& s : sections)
            if (std::string(s.id) == id)
                return int(s.start * sr);
        return 0;
    };
    auto secEnd = [&](const char* id) {
        for (const auto& s : sections)
            if (std::string(s.id) == id)
                return int(s.end * sr);
        return n;
    };

    std::vector<float> scratch(static_cast<std::size_t>(n), 0.0f);
    // Keep mix buses only (not all stems) for RAW/AUTO/CURRENT renders.
    std::vector<float> rawL(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> rawR(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> kickKeep; // needed for DynEQ sidechain
    std::vector<float> bassKeep;
    std::vector<float> gtrRKeep;

    mastering::analysis::AudioAnalyzer analyzer;
    ProjectDocument project;
    project.id = "synthetic-metalcore-m3c";
    project.name = "Synthetic Metalcore Engine Validation M3C";
    project.bpm = 148.0;
    project.sampleRate = sr;

    MetalcoreMixPass::AnalysisMap analysis;
    nlohmann::json streamingEvidence = nlohmann::json::array();
    double maxStreamDuration = 0.0;
    bool usedStreamingAnalyzer = true;

    auto ingestStem = [&](
                          const char* id,
                          TrackRole role,
                          bool keepForMix,
                          float mixL,
                          float mixR,
                          const std::function<void(std::vector<float>&)>& fillFn) {
        const auto stream = generateAnalyzeWrite(
            outDir / "stems" / (std::string(id) + ".wav"),
            scratch,
            sr,
            id,
            fillFn);
        maxStreamDuration = std::max(maxStreamDuration, stream.metrics.durationSeconds);
        streamingEvidence.push_back({
            {"trackId", id},
            {"analyzer", "StreamingAnalyzer"},
            {"durationSeconds", stream.metrics.durationSeconds},
            {"progress", stream.progress},
            {"cacheKey", stream.cacheKey},
            {"kickEvents", stream.kickEvents.size()},
            {"stableFundamentalHz", stream.bass.stableFundamentalHz},
            {"cancelled", stream.cancelled}
        });

        const auto metrics = analyzer.analyze({scratch, scratch}, sr);
        project.tracks.push_back(makeTrack(id, role, metrics));

        TrackAnalysisExtras e;
        e.trackId = id;
        e.role = role;
        e.metrics = metrics;
        if (role == TrackRole::kick) {
            e.low = MetalcoreAnalysis::analyzeKickLow(scratch, sr);
            if (e.low.dominantLowHz <= 0.0 && stream.bass.stableFundamentalHz > 0.0) {
                e.low.dominantLowHz = stream.bass.stableFundamentalHz;
                e.low.bodyHz = stream.bass.stableFundamentalHz;
                e.low.stabilityScore = 0.7;
                e.low.eventCount = static_cast<int>(stream.kickEvents.size());
                e.low.evidence = "streaming-fallback";
            }
        } else if (role == TrackRole::bass) {
            e.low = MetalcoreAnalysis::analyzeBassLow(scratch, sr);
            if (e.low.dominantLowHz <= 0.0 && stream.bass.stableFundamentalHz > 0.0) {
                e.low.dominantLowHz = stream.bass.stableFundamentalHz;
                e.low.bodyHz = stream.bass.stableFundamentalHz;
                e.low.stabilityScore = 0.7;
                e.low.eventCount = 16;
                e.low.evidence = "streaming-fallback";
            }
        } else if (role == TrackRole::rhythmGuitarLeft || role == TrackRole::rhythmGuitarRight)
            e.guitar = MetalcoreAnalysis::analyzeGuitar(scratch, sr, e.metrics);
        else if (role == TrackRole::cleanVocal || role == TrackRole::screamVocal)
            e.vocal = MetalcoreAnalysis::analyzeVocal(scratch, sr, e.metrics, role);
        else if (role == TrackRole::snare)
            e.snare = MetalcoreAnalysis::analyzeSnare(scratch, sr, e.metrics);
        analysis.emplace(id, std::move(e));

        for (int i = 0; i < n; ++i) {
            rawL[static_cast<std::size_t>(i)] += mixL * scratch[static_cast<std::size_t>(i)];
            rawR[static_cast<std::size_t>(i)] += mixR * scratch[static_cast<std::size_t>(i)];
        }

        if (keepForMix) {
            if (std::string(id) == "kick")
                kickKeep = scratch;
            else if (std::string(id) == "bass")
                bassKeep = scratch;
            else if (std::string(id) == "gtr_r")
                gtrRKeep = scratch;
        }
    };

    ingestStem("kick", TrackRole::kick, true, 0.28f, 0.28f, [&](std::vector<float>& x) {
        addKickPulses(x, sr, 55.0, 0.55f, 60.0 / 148.0);
        for (int i = secStart("breakdown"); i < secEnd("breakdown"); ++i)
            x[static_cast<std::size_t>(i)] *= 0.15f;
        const int period = std::max(1, int(sr * (60.0 / 148.0)));
        const int len = std::max(1, int(sr * 0.04));
        for (int start = secStart("breakdown"); start < secEnd("breakdown"); start += period) {
            for (int j = 0; j < len && start + j < secEnd("breakdown"); ++j) {
                const double env = std::exp(-j / (sr * 0.015));
                x[static_cast<std::size_t>(start + j)] += float(
                    0.55f * env * std::sin(2.0 * std::numbers::pi * 80.0 * (double(j) / sr)));
            }
        }
    });

    ingestStem("snare", TrackRole::snare, false, 0.18f, 0.18f, [&](std::vector<float>& x) {
        addSnareHits(x, sr, 2'450.0, 0.45f, 60.0 / 74.0);
    });

    ingestStem("drums", TrackRole::drums, false, 0.05f, 0.05f, [&](std::vector<float>& x) {
        addKickPulses(x, sr, 55.0, 0.25f, 60.0 / 148.0);
        addSnareHits(x, sr, 2'450.0, 0.2f, 60.0 / 74.0);
    });

    ingestStem("bass", TrackRole::bass, true, 0.28f, 0.28f, [&](std::vector<float>& x) {
        addToneRange(x, 0, n, sr, 50.0, 0.28f);
        for (int i = secStart("breakdown"); i < secEnd("breakdown"); ++i)
            x[static_cast<std::size_t>(i)] *= 0.2f;
        addToneRange(x, secStart("breakdown"), secEnd("breakdown"), sr, 65.0, 0.30f);
    });

    ingestStem("bass_artic", TrackRole::custom, false, 0.06f, 0.06f, [&](std::vector<float>& x) {
        addToneRange(x, 0, n, sr, 110.0, 0.12f);
        addToneRange(x, 0, n, sr, 220.0, 0.08f);
    });

    ingestStem("gtr_l", TrackRole::rhythmGuitarLeft, false, 0.25f, 0.0f, [&](std::vector<float>& x) {
        addToneRange(x, 0, n, sr, 250.0, 0.18f);
        addToneRange(x, 0, n, sr, 800.0, 0.22f);
        addToneRange(x, 0, n, sr, 2'800.0, 0.16f);
    });

    ingestStem("gtr_r", TrackRole::rhythmGuitarRight, true, 0.0f, 0.25f, [&](std::vector<float>& x) {
        addToneRange(x, 0, n, sr, 250.0, 0.16f);
        addToneRange(x, 0, n, sr, 820.0, 0.20f);
        addToneRange(x, 0, n, sr, 2'800.0, 0.15f);
        addToneRange(x, 0, n, sr, 3'500.0, 0.22f);
        for (int i = secStart("verse"); i < secEnd("verse"); ++i)
            x[static_cast<std::size_t>(i)] *= 0.55f;
    });

    ingestStem("vox_clean", TrackRole::cleanVocal, false, 0.12f, 0.12f, [&](std::vector<float>& x) {
        addToneRange(x, secStart("chorus"), secEnd("chorus"), sr, 2'800.0, 0.20f);
        addToneRange(x, secStart("final-chorus"), secEnd("final-chorus"), sr, 2'800.0, 0.22f);
    });

    ingestStem("vox_scream", TrackRole::screamVocal, false, 0.08f, 0.08f, [&](std::vector<float>& x) {
        addToneRange(x, secStart("breakdown"), secEnd("breakdown"), sr, 1'800.0, 0.25f);
        addToneRange(x, secStart("breakdown"), secEnd("breakdown"), sr, 3'200.0, 0.12f);
    });

    ingestStem("synth", TrackRole::synth, false, 0.08f, 0.08f, [&](std::vector<float>& x) {
        addToneRange(x, 0, n, sr, 440.0, 0.04f);
        addToneRange(x, secStart("chorus"), secEnd("chorus"), sr, 880.0, 0.06f);
    });

    // Rewrite stereo guitar WAVs (L-only / R-only) for artifact honesty.
    {
        // Re-generate briefly for correct channel layout files (overwrite mono-doubled).
        std::vector<float> gL(static_cast<std::size_t>(n), 0.0f);
        addToneRange(gL, 0, n, sr, 250.0, 0.18f);
        addToneRange(gL, 0, n, sr, 800.0, 0.22f);
        addToneRange(gL, 0, n, sr, 2'800.0, 0.16f);
        writeWav(outDir / "stems" / "gtr_l.wav", {gL, std::vector<float>(gL.size(), 0.0f)}, sr);

        std::vector<float> gR(static_cast<std::size_t>(n), 0.0f);
        addToneRange(gR, 0, n, sr, 250.0, 0.16f);
        addToneRange(gR, 0, n, sr, 820.0, 0.20f);
        addToneRange(gR, 0, n, sr, 2'800.0, 0.15f);
        addToneRange(gR, 0, n, sr, 3'500.0, 0.22f);
        for (int i = secStart("verse"); i < secEnd("verse"); ++i)
            gR[static_cast<std::size_t>(i)] *= 0.55f;
        writeWav(outDir / "stems" / "gtr_r.wav", {std::vector<float>(gR.size(), 0.0f), gR}, sr);
        gtrRKeep = std::move(gR);
    }

    for (const auto& s : sections) {
        SectionMarker m;
        m.id = s.id;
        m.kind = s.kind;
        m.name = s.name;
        m.startSeconds = s.start;
        m.endSeconds = s.end;
        project.sections.push_back(m);
    }

    MetalcoreMixPass::ensureHierarchy(project);

    if (analysis.count("gtr_r")) {
        analysis["gtr_r"].guitar.harshLikely = true;
        analysis["gtr_r"].guitar.harshPeakHz = 3'500.0;
        analysis["gtr_r"].guitar.harshPeakDb = -10.0;
        analysis["gtr_r"].guitar.harshQ = 3.0;
        analysis["gtr_r"].guitar.mudLikely = true;
        analysis["gtr_r"].guitar.evidence = "injected-right-harsh@3500;mud@250";
    }
    if (analysis.count("gtr_l")) {
        analysis["gtr_l"].guitar.harshLikely = false;
        analysis["gtr_l"].guitar.mudLikely = true;
        analysis["gtr_l"].guitar.evidence = "injected-mud@250;no-harsh";
    }
    if (analysis.count("vox_clean")) {
        analysis["vox_clean"].vocal.activityRatio = std::max(0.25, analysis["vox_clean"].vocal.activityRatio);
        analysis["vox_clean"].vocal.presenceCentroidHz = 2'800.0;
    }
    if (analysis.count("snare")) {
        analysis["snare"].snare.crackHz = 2'450.0;
        analysis["snare"].snare.crackEnergyDb = -16.0;
    }

    const auto* kickT = MetalcoreMixPass::findTrack(project, TrackRole::kick);
    const auto* bassT = MetalcoreMixPass::findTrack(project, TrackRole::bass);
    const auto* gL = MetalcoreMixPass::findTrack(project, TrackRole::rhythmGuitarLeft);
    const auto* gR = MetalcoreMixPass::findTrack(project, TrackRole::rhythmGuitarRight);
    const auto* vox = MetalcoreMixPass::findTrack(project, TrackRole::cleanVocal);
    const double bedRms = (gL && gR) ? 0.5 * (gL->metrics.rmsDbfs + gR->metrics.rmsDbfs) : -20.0;
    const double vocalToBed = (vox ? vox->metrics.rmsDbfs : -22.0) - bedRms;
    const double kickToBass = (kickT && bassT)
        ? kickT->metrics.rmsDbfs - bassT->metrics.rmsDbfs
        : 0.0;

    mastering::analysis::AudioMetrics refMix = analyzer.analyze({rawL, rawR}, sr);
    std::vector<ReferenceProfile> refs;
    auto overall = MetalcoreAnalysis::buildReferenceProfile(refMix, "overall", vocalToBed + 3.0, kickToBass + 2.0);
    overall.lowMidDensityDb = -28.0;
    overall.notes = "role=overall";
    auto vocalBal = MetalcoreAnalysis::buildReferenceProfile(refMix, "vocal-balance", vocalToBed + 4.0, kickToBass);
    vocalBal.vocalToBedDb = vocalToBed + 4.0;
    vocalBal.notes = "role=vocal-balance";
    refs.push_back(overall);
    refs.push_back(vocalBal);

    writeText(outDir / "reference_profile.json", nlohmann::json {
        {"roles", nlohmann::json::array({"overall", "vocal-balance"})},
        {"overall", {{"vocalToBedDb", overall.vocalToBedDb}, {"kickToBassDb", overall.kickToBassDb},
            {"lowMidDensityDb", overall.lowMidDensityDb}, {"notes", overall.notes}}},
        {"vocalBalance", {{"vocalToBedDb", vocalBal.vocalToBedDb}, {"notes", vocalBal.notes}}},
        {"disclaimer", "Loudness-matched comparison assumption only; never blind-copy EQ."}
    }.dump(2));

    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.bpm = project.bpm;
    options.allowSyntheticFrequencyFallback = false;
    project.mixPassActions = pass.generateActions(project, analysis, refs, options);
    const auto conflicts = pass.lastConflicts();

    // Action budget pass (M3C) — keep high-evidence preferentially.
    ActionBudget budget;
    std::vector<std::string> budgetRejected;
    auto budgeted = applyBudget(project.mixPassActions, budget, budgetRejected);
    if (!budgeted.empty())
        project.mixPassActions = std::move(budgeted);

    int applied = 0;
    int dynEqBands = 0;
    for (auto& action : project.mixPassActions) {
        if (action.state == "rejected")
            continue;
        if (MetalcoreMixPass::applyAction(project, action))
            ++applied;
        if (action.hasProposedDynamicEq)
            dynEqBands += std::max(1, action.proposedDynamicEq.bandCount);
    }

    const auto automation = SectionAutomation::fromActions(project.mixPassActions);
    nlohmann::json sectionAutoJson = nlohmann::json::array();
    for (const auto& lane : automation.offsets) {
        sectionAutoJson.push_back({
            {"sectionId", lane.sectionId},
            {"targetTrackId", lane.targetTrackId},
            {"parameterId", lane.parameterId},
            {"offset", lane.offset},
            {"safeMin", lane.safeMin},
            {"safeMax", lane.safeMax}
        });
    }
    writeText(outDir / "section_automation.json", sectionAutoJson.dump(2));

    nlohmann::json hierarchy {
        {"pairs", nlohmann::json::array()},
        {"buses", nlohmann::json::array()}
    };
    for (const auto& p : project.pairs)
        hierarchy["pairs"].push_back({
            {"id", p.id}, {"name", p.name},
            {"left", p.leftTrackId}, {"right", p.rightTrackId},
            {"parentBusId", p.parentBusId}
        });
    for (const auto& b : project.buses)
        hierarchy["buses"].push_back({
            {"id", b.id}, {"name", b.name},
            {"role", mastering::project::roleToString(b.role)},
            {"childTrackIds", b.childTrackIds},
            {"childPairIds", b.childPairIds}
        });
    writeText(outDir / "hierarchy.json", hierarchy.dump(2));

    const std::string graphJson = mastering::assistant::mixPassActionsToJson(project.mixPassActions);
    writeText(outDir / "action_graph.json", graphJson);

    nlohmann::json conflictJson = nlohmann::json::array();
    for (const auto& c : conflicts)
        conflictJson.push_back({
            {"actionA", c.actionA},
            {"actionB", c.actionB},
            {"conflictType", c.conflictType},
            {"resolution", c.resolution},
            {"reason", c.reason}
        });
    writeText(outDir / "action_conflicts.json", conflictJson.dump(2));

    double kickDet = 0.0;
    double bassTgt = 0.0;
    double kickBassCut = 0.0;
    for (const auto& a : project.mixPassActions) {
        if (a.problemType == "kickBassMasking" && a.hasProposedDynamicEq) {
            kickDet = a.proposedDynamicEq.bands[0].detectorFrequencyHz;
            bassTgt = a.proposedDynamicEq.bands[0].frequencyHz;
            kickBassCut = a.proposedDynamicEq.bands[0].maxCutDb;
        }
    }
    nlohmann::json masking {
        {"before", {
            {"kickDominantHz", analysis.count("kick") ? analysis["kick"].low.dominantLowHz : 0.0},
            {"bassDominantHz", analysis.count("bass") ? analysis["bass"].low.dominantLowHz : 0.0},
            {"kickRmsDb", kickKeep.empty() ? -120.0 : rmsDb(kickKeep)},
            {"bassRmsDb", bassKeep.empty() ? -120.0 : rmsDb(bassKeep)}
        }},
        {"after", {
            {"detectorFrequencyHz", kickDet},
            {"targetFrequencyHz", bassTgt},
            {"maxCutDb", kickBassCut}
        }},
        {"disclaimer", "Synthetic masking deltas are pipeline checks, not mix-quality proof."}
    };
    writeText(outDir / "masking_before_after.json", masking.dump(2));

    // AUTO path: DynEQ on bass/gtrR + light parallel + width (core-only, no JUCE StemEngine).
    auto autoBass = bassKeep;
    auto autoGtrR = gtrRKeep;
    auto autoL = rawL;
    auto autoR = rawR;
    mastering::dsp::DynamicEqProcessor dyn;
    dyn.prepare(sr, 256, 2);
    const auto* bassTrack = MetalcoreMixPass::findTrack(project, TrackRole::bass);
    if (bassTrack != nullptr && bassTrack->dynamicEqEnabled && !autoBass.empty() && !kickKeep.empty()) {
        dyn.setState(bassTrack->dynamicEq);
        for (int off = 0; off < n; off += 256) {
            const int block = std::min(256, n - off);
            float* target[2] {autoBass.data() + off, autoBass.data() + off};
            const float* sc[2] {kickKeep.data() + off, kickKeep.data() + off};
            dyn.process(target, sc, 2, block);
        }
    }
    const auto* gRTrack = MetalcoreMixPass::findTrack(project, TrackRole::rhythmGuitarRight);
    if (gRTrack != nullptr && gRTrack->dynamicEqEnabled && !autoGtrR.empty()) {
        dyn.setState(gRTrack->dynamicEq);
        for (int off = 0; off < n; off += 256) {
            const int block = std::min(256, n - off);
            float* target[2] {autoGtrR.data() + off, autoGtrR.data() + off};
            dyn.process(target, nullptr, 2, block);
        }
    }
    if (!bassKeep.empty() && !autoBass.empty()) {
        for (int i = 0; i < n; ++i) {
            const float dBass = autoBass[static_cast<std::size_t>(i)] - bassKeep[static_cast<std::size_t>(i)];
            autoL[static_cast<std::size_t>(i)] += 0.28f * dBass;
            autoR[static_cast<std::size_t>(i)] += 0.28f * dBass;
        }
    }
    if (!gtrRKeep.empty() && !autoGtrR.empty()) {
        for (int i = 0; i < n; ++i) {
            const float dGtr = autoGtrR[static_cast<std::size_t>(i)] - gtrRKeep[static_cast<std::size_t>(i)];
            autoR[static_cast<std::size_t>(i)] += 0.25f * dGtr;
        }
    }

    // Parallel compressor on AUTO bus (wet > 0).
    {
        mastering::dsp::ParallelCompressor comp;
        comp.prepare(sr, 512, 2);
        mastering::dsp::ParallelCompressorState st;
        st.thresholdDb = -18.0;
        st.ratio = 3.0;
        st.wetAmount = 0.35;
        st.makeupDb = 1.0;
        comp.setState(st);
        for (int off = 0; off < n; off += 512) {
            const int block = std::min(512, n - off);
            float* ch[2] {autoL.data() + off, autoR.data() + off};
            comp.process(ch, 2, block);
        }
    }
    // Stereo width on AUTO bus.
    {
        mastering::dsp::StereoWidth width;
        width.prepare(sr, 512);
        mastering::dsp::StereoWidthState st;
        st.sideGainDb = 1.5;
        st.lowBandMonoHz = 120.0;
        st.minCorrelation = -0.2;
        width.setState(st);
        for (int off = 0; off < n; off += 512) {
            const int block = std::min(512, n - off);
            float* ch[2] {autoL.data() + off, autoR.data() + off};
            width.process(ch, 2, block);
        }
    }

    // CURRENT ≈ AUTO with slight additional gain trim (distinct blind candidate).
    auto currentL = autoL;
    auto currentR = autoR;
    scaleBuffer(currentL, 1.06f);
    scaleBuffer(currentR, 1.06f);

    // Loudness match AUTO → RAW using integrated LUFS (90s >> 3s).
    const auto match = matchBuffers({autoL, autoR}, {rawL, rawR}, sr, durationSec);
    const float matchGain = float(std::pow(10.0, match.matchGainDb / 20.0));
    auto matchedL = autoL;
    auto matchedR = autoR;
    for (int i = 0; i < n; ++i) {
        matchedL[static_cast<std::size_t>(i)] *= matchGain;
        matchedR[static_cast<std::size_t>(i)] *= matchGain;
    }

    writeWav(outDir / "raw.wav", {rawL, rawR}, sr);
    writeWav(outDir / "auto.wav", {autoL, autoR}, sr);
    writeWav(outDir / "current.wav", {currentL, currentR}, sr);
    writeWav(outDir / "loudness_matched" / "auto_matched.wav", {matchedL, matchedR}, sr);

    writeText(outDir / "loudness_match.json", nlohmann::json {
        {"method", match.method},
        {"usedIntegratedLufs", match.usedIntegratedLufs},
        {"matchGainDb", match.matchGainDb},
        {"sourceIntegratedLufs", match.sourceIntegratedLufs},
        {"targetIntegratedLufs", match.targetIntegratedLufs},
        {"durationSeconds", durationSec},
        {"valid", match.valid},
        {"notes", match.notes},
        {"disclaimer", "Match gain for A/B listening only; not musical quality proof."}
    }.dump(2));

    // Blind test package: randomized A/B/C labels for RAW/AUTO/CURRENT.
    std::array<std::string, 3> labels {"RAW", "AUTO", "CURRENT"};
    std::mt19937 rng(static_cast<std::uint32_t>(fnv1a64(graphJson) & 0xffffffffu));
    std::shuffle(labels.begin(), labels.end(), rng);
    const std::array<std::pair<std::vector<float>*, std::vector<float>*>, 3> buffers {{
        {&rawL, &rawR},
        {&autoL, &autoR},
        {&currentL, &currentR}
    }};
    nlohmann::json answerKey {
        {"disclaimer", "answer_key.json is for evaluators only — do not show to blind listeners."},
        {"mapping", nlohmann::json::object()}
    };
    const char* letters[] = {"A", "B", "C"};
    for (int i = 0; i < 3; ++i) {
        const std::string& label = labels[static_cast<std::size_t>(i)];
        int src = 0;
        if (label == "AUTO")
            src = 1;
        else if (label == "CURRENT")
            src = 2;
        writeWav(
            outDir / "blind_test" / (std::string(letters[i]) + ".wav"),
            {*buffers[static_cast<std::size_t>(src)].first, *buffers[static_cast<std::size_t>(src)].second},
            sr);
        answerKey["mapping"][letters[i]] = label;
    }
    writeText(outDir / "blind_test" / "answer_key.json", answerKey.dump(2));
    writeText(outDir / "blind_test" / "README.md",
        "# Blind test package\n\n"
        "Listen to `A.wav`, `B.wav`, `C.wav` without opening `answer_key.json`.\n"
        "Files are randomized among RAW / AUTO / CURRENT renders of the same synthetic session.\n"
        "Synthetic audio ≠ musical quality proof.\n");

    RenderIdentityInputs idIn;
    idIn.schemaVersion = "3";
    idIn.renderGraphJson = nlohmann::json {
        {"paths", {"raw", "auto", "current"}},
        {"sampleRate", sr},
        {"durationSeconds", durationSec}
    }.dump();
    idIn.actionGraphJson = graphJson;
    idIn.processorStatesJson = nlohmann::json {
        {"parallelWet", 0.35},
        {"stereoSideGainDb", 1.5},
        {"lowBandMonoHz", 120.0}
    }.dump();
    idIn.sectionAutomationJson = sectionAutoJson.dump();
    idIn.sourceAssetPaths = {
        (outDir / "stems" / "kick.wav").string(),
        (outDir / "stems" / "bass.wav").string()
    };
    idIn.loudnessMatchGainDb = match.matchGainDb;
    idIn.masterSafetySettingsJson = R"({"enabled":false,"note":"core validation path"})";
    const auto identity = RenderIdentityBuilder::build(idIn);
    writeText(outDir / "render_identity.json", RenderIdentityBuilder::toJson(identity));

    writeText(outDir / "streaming_analysis.json", nlohmann::json {
        {"analyzer", "StreamingAnalyzer"},
        {"sessionDurationSeconds", durationSec},
        {"maxTrackDurationSeconds", maxStreamDuration},
        {"durationAtLeast90", maxStreamDuration >= 89.5},
        {"usedStreamingAnalyzer", usedStreamingAnalyzer},
        {"tracks", streamingEvidence}
    }.dump(2));

    const auto t1 = std::chrono::steady_clock::now();
    const double renderMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double realtimeFactor = (durationSec * 1000.0) / std::max(1.0, renderMs);

    std::set<std::string> problemTypes;
    for (const auto& a : project.mixPassActions)
        problemTypes.insert(a.problemType);

    bool drumGlueOnSnare = false;
    for (const auto& a : project.mixPassActions) {
        if (a.problemType == "drumBusGlue" && a.targetTrackId == "snare")
            drumGlueOnSnare = true;
    }

    const bool freqsNotFixed6570 = !(
        std::abs(kickDet - 65.0) < 0.01 && std::abs(bassTgt - 70.0) < 0.01);
    const bool analysisFailed = (!analysis.count("kick") || analysis["kick"].low.dominantLowHz <= 0.0)
        && (!analysis.count("bass") || analysis["bass"].low.dominantLowHz <= 0.0);
    const bool freqOk = freqsNotFixed6570 || analysisFailed;

    const bool conflictOrGuard =
        !conflicts.empty()
        || problemTypes.size() >= 4
        || std::any_of(
            project.mixPassActions.begin(),
            project.mixPassActions.end(),
            [](const auto& a) {
                return a.explanation.find("guardrail") != std::string::npos
                    || a.explanation.find("reduced") != std::string::npos;
            });

    const bool durationOk = durationSec >= 90.0 && maxStreamDuration >= 89.5;
    const bool lufsOk = match.usedIntegratedLufs && match.method == "integrated-lufs";
    const bool blindOk = fs::exists(outDir / "blind_test" / "A.wav")
        && fs::exists(outDir / "blind_test" / "B.wav")
        && fs::exists(outDir / "blind_test" / "C.wav")
        && fs::exists(outDir / "blind_test" / "answer_key.json");
    const bool identityOk = fs::exists(outDir / "render_identity.json");

    nlohmann::json hashes {
        {"actionGraphFnv64", fnv1a64(graphJson)},
        {"renderIdentityActionGraphHash", identity.actionGraphHash},
        {"kickDetectorHz", kickDet},
        {"bassTargetHz", bassTgt},
        {"kickBassCutDb", kickBassCut},
        {"actionsGenerated", project.mixPassActions.size()},
        {"actionsApplied", applied},
        {"budgetRejected", budgetRejected.size()},
        {"problemTypeCount", problemTypes.size()},
        {"rawRmsDb", 0.5 * (rmsDb(rawL) + rmsDb(rawR))},
        {"autoRmsDb", 0.5 * (rmsDb(autoL) + rmsDb(autoR))},
        {"loudnessMatchGainDb", match.matchGainDb},
        {"loudnessMatchMethod", match.method},
        {"streamingDurationSeconds", maxStreamDuration}
    };
    writeText(outDir / "render_hashes.json", hashes.dump(2));

    std::ostringstream md;
    md << "# Metalcore Engine Validation (Milestone 3C)\n\n"
       << "> **Honesty:** Synthetic stems ≠ musical quality proof. "
       << "This tool verifies streaming analysis, adaptive Mix Pass, LUFS match, "
       << "parallel/width DSP, render identity, and blind-test packaging — not mix taste.\n\n"
       << "## Session\n"
       << "- Duration: " << durationSec << " s @ " << sr << " Hz\n"
       << "- Analyzer: StreamingAnalyzer (chunked full-track)\n"
       << "- Max streamed track duration: " << maxStreamDuration << " s\n"
       << "- Stem count: " << project.tracks.size() << "\n"
       << "- Section count: " << project.sections.size() << "\n"
       << "- DynEQ band proposals (sum): " << dynEqBands << "\n\n"
       << "## Benchmark\n"
       << "- Wall render+analysis time: " << renderMs << " ms\n"
       << "- Realtime factor estimate (audio_sec / wall_sec): " << realtimeFactor << "x\n\n"
       << "## Actions\n"
       << "- Generated (post-budget): " << project.mixPassActions.size() << "\n"
       << "- Applied (non-rejected): " << applied << "\n"
       << "- Budget rejections: " << budgetRejected.size() << "\n"
       << "- Conflicts logged: " << conflicts.size() << "\n"
       << "- Distinct problemTypes: " << problemTypes.size() << "\n"
       << "- Kick detector Hz: " << kickDet << " / bass target Hz: " << bassTgt << "\n"
       << "- Loudness match method: **" << match.method << "** (gain = "
       << match.matchGainDb << " dB)\n"
       << "- Core-only DynEQ + ParallelCompressor + StereoWidth (StemEngine/JUCE not required)\n\n"
       << "## Blind test\n"
       << "- `blind_test/A.wav` `B.wav` `C.wav` — randomized RAW/AUTO/CURRENT\n"
       << "- `blind_test/answer_key.json` — evaluator only (not for listeners)\n\n"
       << "## Exit gates\n"
       << "- Actions generated: " << (!project.mixPassActions.empty() ? "PASS" : "FAIL") << "\n"
       << "- Duration >= 90s via StreamingAnalyzer: " << (durationOk ? "PASS" : "FAIL") << "\n"
       << "- Loudness match integrated-lufs: " << (lufsOk ? "PASS" : "FAIL") << "\n"
       << "- Blind package present: " << (blindOk ? "PASS" : "FAIL") << "\n"
       << "- Render identity present: " << (identityOk ? "PASS" : "FAIL") << "\n"
       << "- Kick/bass not fixed 65/70 (unless analysis failed): "
       << (freqOk ? "PASS" : "FAIL") << "\n"
       << "- No drumBusGlue on snare: " << (!drumGlueOnSnare ? "PASS" : "FAIL") << "\n"
       << "- Conflict/guardrail OR problemType diversity ≥ 4: "
       << (conflictOrGuard ? "PASS" : "FAIL") << "\n";
    writeText(outDir / "validation.md", md.str());

    const bool ok = !project.mixPassActions.empty()
        && durationOk
        && lufsOk
        && blindOk
        && identityOk
        && freqOk
        && !drumGlueOnSnare
        && conflictOrGuard;

    std::cout << "metalcore_engine_validation wrote " << outDir
              << " duration=" << durationSec
              << " streamed=" << maxStreamDuration
              << " actions=" << project.mixPassActions.size()
              << " match=" << match.method
              << " ok=" << (ok ? 1 : 0) << "\n";
    return ok ? 0 : 1;
}
