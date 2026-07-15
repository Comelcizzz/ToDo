#include "mastering/benchmark/BenchmarkSessionManifest.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {
namespace {

using json = nlohmann::json;

std::string toLower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsToken(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

} // namespace

std::string serializeManifest(const BenchmarkSessionManifest& m)
{
    json j;
    j["schemaVersion"] = m.schemaVersion;
    j["sessionId"] = m.sessionId;
    j["projectName"] = m.projectName;
    j["artistAlias"] = m.artistAlias;
    j["bpm"] = m.bpm;
    j["tempoMapJson"] = m.tempoMapJson;
    j["sampleRate"] = m.sampleRate;
    j["bitDepth"] = m.bitDepth;
    j["expectedDurationSeconds"] = m.expectedDurationSeconds;
    j["timeSignature"] = m.timeSignature;
    j["referencePaths"] = m.referencePaths;
    j["targetMixPath"] = m.targetMixPath;
    j["desiredCharacterNotes"] = m.desiredCharacterNotes;
    j["excludedProcessors"] = m.excludedProcessors;
    j["localOnly"] = m.localOnly;
    j["profileId"] = m.profileId;
    j["engineVersion"] = m.engineVersion;
    j["createdAt"] = m.createdAt;

    j["stems"] = json::array();
    for (const auto& s : m.stems) {
        j["stems"].push_back({
            {"assetId", s.assetId},
            {"filePath", s.filePath},
            {"contentFingerprint", s.contentFingerprint},
            {"role", project::roleToString(s.role)},
            {"subRole", s.subRole},
            {"mono", s.mono},
            {"pairId", s.pairId},
            {"parentBusId", s.parentBusId},
            {"gainDb", s.gainDb},
            {"pan", s.pan},
            {"polarityInverted", s.polarityInverted},
            {"startOffsetSeconds", s.startOffsetSeconds},
            {"userNotes", s.userNotes},
            {"channelCount", s.channelCount},
            {"sampleRate", s.sampleRate},
            {"durationSeconds", s.durationSeconds}});
    }
    j["pairs"] = json::array();
    for (const auto& p : m.pairs) {
        j["pairs"].push_back({
            {"id", p.id},
            {"name", p.name},
            {"leftAssetId", p.leftAssetId},
            {"rightAssetId", p.rightAssetId},
            {"parentBusId", p.parentBusId}});
    }
    j["buses"] = json::array();
    for (const auto& b : m.buses) {
        j["buses"].push_back({
            {"id", b.id},
            {"name", b.name},
            {"role", project::roleToString(b.role)},
            {"childAssetIds", b.childAssetIds},
            {"childPairIds", b.childPairIds}});
    }
    j["sections"] = json::array();
    for (const auto& s : m.sections) {
        j["sections"].push_back({
            {"id", s.id},
            {"kind", project::sectionKindToString(s.kind)},
            {"name", s.name},
            {"startSeconds", s.startSeconds},
            {"endSeconds", s.endSeconds}});
    }
    j["expectedProblems"] = json::array();
    for (const auto& e : m.expectedProblems) {
        j["expectedProblems"].push_back({
            {"problemId", e.problemId},
            {"category", e.category},
            {"targetAssetId", e.targetAssetId},
            {"sectionId", e.sectionId},
            {"notes", e.notes},
            {"evaluationOnly", true}});
    }
    return j.dump(2);
}

std::optional<BenchmarkSessionManifest> deserializeManifest(std::string_view source, std::string* error)
{
    try {
        const auto j = json::parse(source);
        BenchmarkSessionManifest m;
        m.schemaVersion = j.value("schemaVersion", m.schemaVersion);
        m.sessionId = j.value("sessionId", m.sessionId);
        m.projectName = j.value("projectName", m.projectName);
        m.artistAlias = j.value("artistAlias", m.artistAlias);
        m.bpm = j.value("bpm", m.bpm);
        m.tempoMapJson = j.value("tempoMapJson", m.tempoMapJson);
        m.sampleRate = j.value("sampleRate", m.sampleRate);
        m.bitDepth = j.value("bitDepth", m.bitDepth);
        m.expectedDurationSeconds = j.value("expectedDurationSeconds", m.expectedDurationSeconds);
        m.timeSignature = j.value("timeSignature", m.timeSignature);
        m.targetMixPath = j.value("targetMixPath", m.targetMixPath);
        m.desiredCharacterNotes = j.value("desiredCharacterNotes", m.desiredCharacterNotes);
        m.localOnly = j.value("localOnly", true);
        m.profileId = j.value("profileId", m.profileId);
        m.engineVersion = j.value("engineVersion", m.engineVersion);
        m.createdAt = j.value("createdAt", m.createdAt);
        if (const auto it = j.find("referencePaths"); it != j.end() && it->is_array())
            m.referencePaths = it->get<std::vector<std::string>>();
        if (const auto it = j.find("excludedProcessors"); it != j.end() && it->is_array())
            m.excludedProcessors = it->get<std::vector<std::string>>();

        if (const auto it = j.find("stems"); it != j.end() && it->is_array()) {
            for (const auto& sj : *it) {
                BenchmarkStemEntry s;
                s.assetId = sj.value("assetId", "");
                s.filePath = sj.value("filePath", "");
                s.contentFingerprint = sj.value("contentFingerprint", "");
                if (const auto role = project::roleFromString(sj.value("role", "custom")))
                    s.role = *role;
                s.subRole = sj.value("subRole", "");
                s.mono = sj.value("mono", false);
                s.pairId = sj.value("pairId", "");
                s.parentBusId = sj.value("parentBusId", "");
                s.gainDb = sj.value("gainDb", 0.0);
                s.pan = sj.value("pan", 0.0);
                s.polarityInverted = sj.value("polarityInverted", false);
                s.startOffsetSeconds = sj.value("startOffsetSeconds", 0.0);
                s.userNotes = sj.value("userNotes", "");
                s.channelCount = sj.value("channelCount", 0);
                s.sampleRate = sj.value("sampleRate", 0.0);
                s.durationSeconds = sj.value("durationSeconds", 0.0);
                m.stems.push_back(std::move(s));
            }
        }
        if (const auto it = j.find("pairs"); it != j.end() && it->is_array()) {
            for (const auto& pj : *it) {
                BenchmarkPairEntry p;
                p.id = pj.value("id", "");
                p.name = pj.value("name", "");
                p.leftAssetId = pj.value("leftAssetId", "");
                p.rightAssetId = pj.value("rightAssetId", "");
                p.parentBusId = pj.value("parentBusId", "");
                m.pairs.push_back(std::move(p));
            }
        }
        if (const auto it = j.find("buses"); it != j.end() && it->is_array()) {
            for (const auto& bj : *it) {
                BenchmarkBusEntry b;
                b.id = bj.value("id", "");
                b.name = bj.value("name", "");
                if (const auto role = project::roleFromString(bj.value("role", "custom")))
                    b.role = *role;
                if (bj.contains("childAssetIds"))
                    b.childAssetIds = bj.at("childAssetIds").get<std::vector<std::string>>();
                if (bj.contains("childPairIds"))
                    b.childPairIds = bj.at("childPairIds").get<std::vector<std::string>>();
                m.buses.push_back(std::move(b));
            }
        }
        if (const auto it = j.find("sections"); it != j.end() && it->is_array()) {
            for (const auto& sj : *it) {
                BenchmarkSectionEntry s;
                s.id = sj.value("id", "");
                if (const auto kind = project::sectionKindFromString(sj.value("kind", "custom")))
                    s.kind = *kind;
                s.name = sj.value("name", "");
                s.startSeconds = sj.value("startSeconds", 0.0);
                s.endSeconds = sj.value("endSeconds", 0.0);
                m.sections.push_back(std::move(s));
            }
        }
        if (const auto it = j.find("expectedProblems"); it != j.end() && it->is_array()) {
            for (const auto& ej : *it) {
                ExpectedProblemAnnotation e;
                e.problemId = ej.value("problemId", "");
                e.category = ej.value("category", "");
                e.targetAssetId = ej.value("targetAssetId", "");
                e.sectionId = ej.value("sectionId", "");
                e.notes = ej.value("notes", "");
                m.expectedProblems.push_back(std::move(e));
            }
        }
        return m;
    } catch (const std::exception& ex) {
        if (error != nullptr)
            *error = ex.what();
        return std::nullopt;
    }
}

std::vector<std::string> validateManifestStructure(const BenchmarkSessionManifest& m)
{
    std::vector<std::string> errors;
    if (m.schemaVersion != kBenchmarkManifestSchemaVersion)
        errors.push_back("unsupported schemaVersion");
    if (m.sessionId.empty())
        errors.push_back("sessionId required");
    if (m.stems.empty())
        errors.push_back("at least one stem required");

    std::vector<std::string> ids;
    for (const auto& s : m.stems) {
        if (s.assetId.empty())
            errors.push_back("stem missing assetId");
        if (s.filePath.empty())
            errors.push_back("stem " + s.assetId + " missing filePath");
        ids.push_back(s.assetId);
    }
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] == ids[i - 1])
            errors.push_back("duplicate assetId: " + ids[i]);
    }

    for (const auto& p : m.pairs) {
        if (p.leftAssetId.empty() || p.rightAssetId.empty())
            errors.push_back("pair " + p.id + " missing L/R asset");
    }

    // Simple cycle check on buses: a bus must not list itself as child.
    for (const auto& b : m.buses) {
        for (const auto& child : b.childAssetIds) {
            if (child == b.id)
                errors.push_back("routing cycle: bus " + b.id + " lists itself");
        }
    }
    return errors;
}

project::TrackRole suggestRoleFromFilename(std::string_view filename)
{
    return project::inferRoleFromFilename(filename);
}

double roleSuggestionConfidence(std::string_view filename, project::TrackRole role)
{
    const auto suggested = suggestRoleFromFilename(filename);
    if (suggested == role && role != project::TrackRole::custom)
        return 0.85;
    const auto lower = toLower(std::string {filename});
    if (role == project::TrackRole::kick && containsToken(lower, "kick"))
        return 0.8;
    if (role == project::TrackRole::snare && containsToken(lower, "snare"))
        return 0.8;
    if (role == project::TrackRole::bass && containsToken(lower, "bass"))
        return 0.75;
    if ((role == project::TrackRole::rhythmGuitarLeft || role == project::TrackRole::rhythmGuitarRight)
        && (containsToken(lower, "gtr") || containsToken(lower, "guitar")))
        return 0.7;
    if (role == project::TrackRole::custom)
        return 0.2;
    return 0.35;
}

} // namespace mastering::benchmark
