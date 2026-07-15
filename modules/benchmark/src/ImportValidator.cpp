#include "mastering/benchmark/ImportValidator.h"

#include "mastering/analysis/AnalysisFingerprint.h"
#include "mastering/analysis/Sha256.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {
namespace {

namespace fs = std::filesystem;

ImportIssueSeverity worst(ImportIssueSeverity a, ImportIssueSeverity b)
{
    return static_cast<int>(a) > static_cast<int>(b) ? a : b;
}

bool isAudioExt(const fs::path& p)
{
    const auto e = p.extension().string();
    return e == ".wav" || e == ".WAV" || e == ".aif" || e == ".aiff" || e == ".AIF" || e == ".AIFF";
}

// Minimal WAV header probe (no full decode) for CI-friendly validation.
struct WavProbe {
    bool ok {false};
    int channels {0};
    int sampleRate {0};
    int bitsPerSample {0};
    double durationSeconds {0.0};
    bool hasData {false};
    std::string error;
};

WavProbe probeWav(const fs::path& path)
{
    WavProbe out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.error = "unreadable";
        return out;
    }
    char riff[12] {};
    in.read(riff, 12);
    if (in.gcount() < 12 || std::string(riff, 4) != "RIFF" || std::string(riff + 8, 4) != "WAVE") {
        out.error = "not RIFF/WAVE";
        return out;
    }
    std::uint32_t dataBytes = 0;
    while (in) {
        char id[4] {};
        std::uint32_t size = 0;
        in.read(id, 4);
        in.read(reinterpret_cast<char*>(&size), 4);
        if (!in)
            break;
        const std::string tag(id, 4);
        if (tag == "fmt ") {
            std::uint16_t audioFormat = 0, channels = 0, bits = 0;
            std::uint32_t sr = 0;
            in.read(reinterpret_cast<char*>(&audioFormat), 2);
            in.read(reinterpret_cast<char*>(&channels), 2);
            in.read(reinterpret_cast<char*>(&sr), 4);
            in.ignore(6);
            in.read(reinterpret_cast<char*>(&bits), 2);
            if (size > 16)
                in.ignore(static_cast<std::streamsize>(size - 16));
            out.channels = channels;
            out.sampleRate = static_cast<int>(sr);
            out.bitsPerSample = bits;
            if (audioFormat != 1 && audioFormat != 3) {
                out.error = "unsupported WAV format";
                return out;
            }
        } else if (tag == "data") {
            dataBytes = size;
            out.hasData = true;
            in.ignore(static_cast<std::streamsize>(size));
        } else {
            in.ignore(static_cast<std::streamsize>(size));
        }
    }
    if (!out.hasData || out.sampleRate <= 0 || out.channels <= 0) {
        out.error = out.error.empty() ? "missing fmt/data" : out.error;
        return out;
    }
    const double bytesPerSec =
        double(out.sampleRate) * double(out.channels) * double(std::max(1, out.bitsPerSample / 8));
    out.durationSeconds = bytesPerSec > 0.0 ? double(dataBytes) / bytesPerSec : 0.0;
    out.ok = true;
    return out;
}

} // namespace

std::string severityToString(ImportIssueSeverity s) noexcept
{
    switch (s) {
    case ImportIssueSeverity::pass:
        return "PASS";
    case ImportIssueSeverity::warning:
        return "WARNING";
    case ImportIssueSeverity::error:
        return "ERROR";
    case ImportIssueSeverity::userDecisionRequired:
        return "USER_DECISION_REQUIRED";
    }
    return "WARNING";
}

std::string serializeImportReport(const ImportValidationReport& report)
{
    nlohmann::json j;
    j["overall"] = severityToString(report.overall);
    j["commonDurationSeconds"] = report.commonDurationSeconds;
    j["longestDurationSeconds"] = report.longestDurationSeconds;
    j["alignmentAmbiguous"] = report.alignmentAmbiguous;
    j["issues"] = nlohmann::json::array();
    for (const auto& issue : report.issues) {
        j["issues"].push_back({
            {"severity", severityToString(issue.severity)},
            {"code", issue.code},
            {"assetId", issue.assetId},
            {"message", issue.message},
            {"suggestedAction", issue.suggestedAction}});
    }
    return j.dump(2);
}

ImportValidationReport validateImport(
    const BenchmarkSessionManifest& manifest,
    const std::string& sessionRoot)
{
    ImportValidationReport report;
    for (const auto& err : validateManifestStructure(manifest)) {
        report.issues.push_back({ImportIssueSeverity::error, "manifest", "", err, "fix-manifest"});
        report.overall = worst(report.overall, ImportIssueSeverity::error);
    }

    std::unordered_set<std::string> fingerprints;
    std::unordered_set<std::string> assetIds;
    double minDur = 1.0e9;
    double maxDur = 0.0;
    bool anyOffset = false;

    for (const auto& stem : manifest.stems) {
        if (!assetIds.insert(stem.assetId).second) {
            report.issues.push_back({
                ImportIssueSeverity::error,
                "duplicate-asset-id",
                stem.assetId,
                "Duplicate asset ID",
                "rename-asset"});
            report.overall = worst(report.overall, ImportIssueSeverity::error);
        }

        fs::path path = fs::path(stem.filePath);
        if (!path.is_absolute())
            path = fs::path(sessionRoot) / path;

        if (!fs::exists(path)) {
            report.issues.push_back({
                ImportIssueSeverity::error,
                "missing-file",
                stem.assetId,
                "File does not exist: " + path.string(),
                "locate-file"});
            report.overall = worst(report.overall, ImportIssueSeverity::error);
            continue;
        }
        if (!isAudioExt(path)) {
            report.issues.push_back({
                ImportIssueSeverity::error,
                "unsupported-format",
                stem.assetId,
                "Supported formats: WAV/AIFF",
                "convert"});
            report.overall = worst(report.overall, ImportIssueSeverity::error);
            continue;
        }

        const auto probe = probeWav(path);
        if (!probe.ok) {
            // AIFF or corrupt — mark error without claiming decode.
            report.issues.push_back({
                ImportIssueSeverity::error,
                "unreadable-or-corrupt",
                stem.assetId,
                probe.error.empty() ? "Could not probe audio header" : probe.error,
                "replace-file"});
            report.overall = worst(report.overall, ImportIssueSeverity::error);
            continue;
        }

        if (manifest.sampleRate > 0.0
            && std::abs(double(probe.sampleRate) - manifest.sampleRate) > 0.5) {
            report.issues.push_back({
                ImportIssueSeverity::error,
                "sample-rate-mismatch",
                stem.assetId,
                "Stem SR " + std::to_string(probe.sampleRate) + " != session "
                    + std::to_string(manifest.sampleRate),
                "resample-or-split-session"});
            report.overall = worst(report.overall, ImportIssueSeverity::error);
        }

        if (probe.durationSeconds < 0.05) {
            report.issues.push_back({
                ImportIssueSeverity::warning,
                "silent-or-very-short",
                stem.assetId,
                "Duration < 50 ms — may be silent/empty",
                "review"});
            report.overall = worst(report.overall, ImportIssueSeverity::warning);
        }

        if (stem.role == project::TrackRole::custom) {
            report.issues.push_back({
                ImportIssueSeverity::warning,
                "missing-role",
                stem.assetId,
                "Role is custom/unassigned",
                "confirm-role"});
            report.overall = worst(report.overall, ImportIssueSeverity::warning);
        }

        if (std::abs(stem.startOffsetSeconds) > 1.0e-6) {
            anyOffset = true;
            report.issues.push_back({
                ImportIssueSeverity::userDecisionRequired,
                "start-offset",
                stem.assetId,
                "Non-zero start offset — will NOT auto-align",
                "preserve-offset"});
            report.overall = worst(report.overall, ImportIssueSeverity::userDecisionRequired);
        }

        // Content fingerprint for duplicate detection (file bytes SHA-256).
        const auto fp = analysis::sha256FileHex(path.string());
        if (!fingerprints.insert(fp).second) {
            report.issues.push_back({
                ImportIssueSeverity::warning,
                "duplicate-file",
                stem.assetId,
                "Identical content fingerprint to another stem",
                "review-duplicate"});
            report.overall = worst(report.overall, ImportIssueSeverity::warning);
        }

        minDur = std::min(minDur, probe.durationSeconds);
        maxDur = std::max(maxDur, probe.durationSeconds);
    }

    for (const auto& pair : manifest.pairs) {
        bool left = false, right = false;
        for (const auto& s : manifest.stems) {
            if (s.assetId == pair.leftAssetId)
                left = true;
            if (s.assetId == pair.rightAssetId)
                right = true;
        }
        if (!left || !right) {
            report.issues.push_back({
                ImportIssueSeverity::error,
                "missing-pair-partner",
                pair.id,
                "Pair missing L and/or R stem",
                "fix-pair"});
            report.overall = worst(report.overall, ImportIssueSeverity::error);
        }
    }

    // Detect L without R from naming — warning / user decision.
    for (const auto& s : manifest.stems) {
        if (s.role == project::TrackRole::rhythmGuitarLeft && s.pairId.empty()) {
            bool hasRight = false;
            for (const auto& o : manifest.stems) {
                if (o.role == project::TrackRole::rhythmGuitarRight)
                    hasRight = true;
            }
            if (!hasRight) {
                report.issues.push_back({
                    ImportIssueSeverity::userDecisionRequired,
                    "missing-pair-partner",
                    s.assetId,
                    "Guitar L present without Guitar R — no automatic alignment/pairing",
                    "confirm-pair-or-leave"});
                report.overall = worst(report.overall, ImportIssueSeverity::userDecisionRequired);
            }
        }
    }

    if (maxDur > 0.0 && minDur < 1.0e8 && (maxDur - minDur) > 0.25) {
        report.issues.push_back({
            ImportIssueSeverity::userDecisionRequired,
            "inconsistent-lengths",
            "",
            "Stem durations differ by >250 ms",
            "pad|truncate|use-longest"});
        report.alignmentAmbiguous = true;
        report.overall = worst(report.overall, ImportIssueSeverity::userDecisionRequired);
    }

    report.longestDurationSeconds = maxDur > 0.0 ? maxDur : 0.0;
    report.commonDurationSeconds = (minDur < 1.0e8) ? minDur : 0.0;
    if (anyOffset)
        report.alignmentAmbiguous = true;

    // Policy note: never auto time/phase-align independent guitar doubles.
    report.issues.push_back({
        ImportIssueSeverity::pass,
        "no-auto-guitar-align",
        "",
        "Independent guitar doubles are never automatically time/phase aligned",
        "preserve-offset"});

    if (report.issues.empty())
        report.overall = ImportIssueSeverity::pass;
    return report;
}

BenchmarkSessionManifest makeReadinessFixtureManifest(const std::string& fixtureRoot)
{
    BenchmarkSessionManifest m;
    m.sessionId = "readiness-fixture-v1";
    m.projectName = "Real-session readiness smoke fixture";
    m.artistAlias = "synthetic-local";
    m.bpm = 140.0;
    m.sampleRate = 48'000.0;
    m.bitDepth = 24;
    m.expectedDurationSeconds = 8.0;
    m.localOnly = true;
    m.profileId = "modern-metalcore-balanced";
    m.desiredCharacterNotes = "Non-copyrighted fixture for import validation";

    auto add = [&](const char* id, const char* file, project::TrackRole role, bool mono, double offset = 0.0) {
        BenchmarkStemEntry s;
        s.assetId = id;
        s.filePath = std::string(fixtureRoot) + "/" + file;
        s.role = role;
        s.mono = mono;
        s.startOffsetSeconds = offset;
        m.stems.push_back(std::move(s));
    };

    add("kick", "kick.wav", project::TrackRole::kick, true);
    add("snare", "snare.wav", project::TrackRole::snare, true);
    add("bass", "bass.wav", project::TrackRole::bass, true);
    add("gtr_l", "Gtr_L_Rhythm.wav", project::TrackRole::rhythmGuitarLeft, false);
    // Missing pair partner intentionally omitted (gtr_r absent).
    add("vox", "LeadVox.wav", project::TrackRole::cleanVocal, true);
    add("silent", "silent_pad.wav", project::TrackRole::effects, true);
    add("short", "short_fx.wav", project::TrackRole::effects, true, 0.05);
    add("sr_mismatch", "kick_96k.wav", project::TrackRole::custom, true); // 96k vs 48k session

    m.referencePaths.push_back(std::string(fixtureRoot) + "/reference.wav");
    m.targetMixPath = std::string(fixtureRoot) + "/target_mix.wav";

    ExpectedProblemAnnotation ann;
    ann.problemId = "exp-kick-bass";
    ann.category = "kick/bass";
    ann.targetAssetId = "bass";
    ann.notes = "Evaluation-only annotation";
    m.expectedProblems.push_back(std::move(ann));

    BenchmarkSectionEntry sec;
    sec.id = "chorus";
    sec.kind = project::SectionKind::chorus;
    sec.name = "Chorus";
    sec.startSeconds = 2.0;
    sec.endSeconds = 6.0;
    m.sections.push_back(std::move(sec));
    return m;
}

} // namespace mastering::benchmark
