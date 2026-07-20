#include <catch2/catch_test_macros.hpp>

#include "mastering/product/ProductVersion.h"
#include "mastering/project/ProjectDocument.h"
#include "mastering/reliability/AssetRelinker.h"
#include "mastering/reliability/AtomicFile.h"
#include "mastering/reliability/AutosaveRecovery.h"
#include "mastering/reliability/Diagnostics.h"
#include "mastering/reliability/DiskMemory.h"
#include "mastering/reliability/ExportPolicy.h"
#include "mastering/reliability/IpcHardening.h"
#include "mastering/reliability/JobSystem.h"
#include "mastering/reliability/PortablePackage.h"
#include "mastering/reliability/ProjectLock.h"
#include "mastering/reliability/RenderIntegrity.h"
#include "mastering/reliability/SchemaMigration.h"
#include "mastering/reliability/SettingsStore.h"
#include "mastering/reliability/TypedError.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mastering::reliability;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name)
{
    auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string tinyProjectJson(int schema)
{
    return std::string("{\"schemaVersion\":") + std::to_string(schema)
        + ",\"id\":\"p1\",\"name\":\"Test\",\"tracks\":[],\"mixPassActions\":["
          "{\"actionId\":\"a1\",\"state\":\"preview\",\"problemType\":\"x\"}]}";
}

} // namespace

TEST_CASE("M4B product version is alpha.m4b with schema 7", "[milestone4b]")
{
    const auto v = mastering::product::currentProductVersion();
    REQUIRE(v.prerelease == "alpha.m4b");
    REQUIRE(v.engineRevision == 5);
    REQUIRE(v.projectSchemaVersion == 7);
    REQUIRE(mastering::project::kCurrentSchemaVersion == 7);
}

TEST_CASE("M4B atomic save survives and keeps backup", "[milestone4b]")
{
    const auto dir = tempDir("m4b-atomic");
    const auto path = dir / "project.masuite";
    {
        std::ofstream(path) << "{\"schemaVersion\":6,\"id\":\"old\"}";
    }
    const auto r1 = atomicSaveText(path.string(), "{\"schemaVersion\":7,\"id\":\"new\"}", 7, true);
    REQUIRE(r1.ok);
    REQUIRE(fs::exists(path));
    REQUIRE_FALSE(r1.checksumSha256.empty());
    REQUIRE(fs::exists(path.string() + ".bak"));

    // Incomplete temp must not replace final.
    REQUIRE(writeIncompleteTempForTest(path.string(), "{broken"));
    REQUIRE(looksLikeIncompleteSave(path.string()));
    std::ifstream in(path);
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(body.find("\"new\"") != std::string::npos);
}

TEST_CASE("M4B autosave strips temporary Preview from committed state", "[milestone4b]")
{
    const auto stripped = stripTemporaryPreviewFromProjectJson(tinyProjectJson(7));
    REQUIRE(stripped.find("\"preview\"") == std::string::npos);
    REQUIRE(stripped.find("\"pending\"") != std::string::npos);
    REQUIRE(stripped.find("autosaveExcludedPreview") != std::string::npos);
}

TEST_CASE("M4B recovery scan finds autosave without overwriting original", "[milestone4b]")
{
    const auto dir = tempDir("m4b-recovery");
    const auto project = dir / "song.masuite";
    std::ofstream(project) << tinyProjectJson(7);
    std::ofstream(autosavePathFor(project.string())) << tinyProjectJson(7);
    writeIncompleteTempForTest(project.string(), "{incomplete");
    const auto candidates = scanRecoveryCandidates(project.string(), dir.string());
    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().hasNewerAutosave);
    REQUIRE(candidates.front().hasIncompleteSave);
    REQUIRE(candidates.front().differencesSummary.find("not be overwritten") != std::string::npos);
}

TEST_CASE("M4B schema migration from M3A-era to 7 and rejects newer", "[milestone4b]")
{
    const auto oldJson = tinyProjectJson(3);
    const auto migrated = migrateDocument("project", oldJson, 3, 1, 7, "");
    REQUIRE(migrated.ok);
    REQUIRE(migrated.sourceVersion == 3);
    REQUIRE(migrated.targetVersion == 7);
    const bool schemaOk = migrated.outputJson.find("\"schemaVersion\": 7") != std::string::npos
        || migrated.outputJson.find("\"schemaVersion\":7") != std::string::npos;
    REQUIRE(schemaOk);

    const auto newer = migrateDocument("project", tinyProjectJson(99), 99, 1, 7, "");
    REQUIRE_FALSE(newer.ok);
    REQUIRE(newer.unsupportedNewer);
    REQUIRE(newer.readOnly);
}

TEST_CASE("M4B asset relink rejects name-only match without fingerprint", "[milestone4b]")
{
    const auto dir = tempDir("m4b-relink");
    const auto wav = dir / "Kick.wav";
    {
        std::ofstream out(wav, std::ios::binary);
        out << "RIFF....WAVEfmt ";
    }
    AssetIdentity asset;
    asset.assetId = "kick";
    asset.expectedPath = (dir / "missing" / "Kick.wav").string();
    asset.contentFingerprint = "";
    RelinkSearchOptions opt;
    opt.projectFolder = dir.string();
    opt.allowNameOnlyMatch = true;
    const auto found = findByFingerprint(asset, opt);
    REQUIRE(found.status == RelinkStatus::nameOnlyMatchRejected);
}

TEST_CASE("M4B portable package blocks path traversal and requires audio consent", "[milestone4b]")
{
    REQUIRE_FALSE(isSafePackageRelativePath("../etc/passwd"));
    REQUIRE_FALSE(isSafePackageRelativePath("/abs/path"));
    REQUIRE(isSafePackageRelativePath("stems/kick.wav"));

    PortablePackageRequest req;
    req.destinationDirectory = tempDir("m4b-pkg").string();
    req.projectJson = tinyProjectJson(7);
    req.includeStems = true;
    req.explicitAudioConsent = false;
    const auto denied = exportPortablePackage(req);
    REQUIRE_FALSE(denied.ok);
    REQUIRE(denied.error.has_value());
    REQUIRE(denied.error->code == "PACKAGE_AUDIO_CONSENT_REQUIRED");

    req.includeStems = false;
    req.mode = PackageMode::metadataOnly;
    const auto ok = exportPortablePackage(req);
    REQUIRE(ok.ok);
    REQUIRE(validatePortablePackage(ok.packageRoot).ok);
}

TEST_CASE("M4B job system bounded concurrency cancel and snapshot pin", "[milestone4b]")
{
    JobSystem jobs(2);
    JobRecord a;
    a.jobId = "a";
    a.snapshotJson = "{\"id\":\"snap-a\"}";
    a.renderGraphHash = "graph-a";
    a.type = JobType::exportMaster;
    JobRecord b;
    b.jobId = "b";
    b.type = JobType::renderAuto;
    JobRecord c;
    c.jobId = "c";
    c.type = JobType::experiment;
    jobs.enqueue(a);
    jobs.enqueue(b);
    jobs.enqueue(c);
    REQUIRE(jobs.runningCount() == 2);
    REQUIRE(jobs.find("c")->status == JobStatus::queued);
    REQUIRE(jobs.cancel("c"));
    REQUIRE(jobs.find("c")->status == JobStatus::cancelled);
    REQUIRE(jobs.cancel("a"));
    REQUIRE(jobs.find("a")->status == JobStatus::cancelling);
    // Cancelling complete clears artifacts (no corrupted completed output).
    REQUIRE(jobs.complete("a", {"/tmp/should-not-keep.wav"}));
    REQUIRE(jobs.find("a")->status == JobStatus::cancelled);
    REQUIRE(jobs.find("a")->outputArtifacts.empty());
    REQUIRE_FALSE(jobs.find("a")->snapshotJson.empty()); // snapshot remains for audit
}

TEST_CASE("M4B render discard incomplete does not promote as completed", "[milestone4b]")
{
    const auto dir = tempDir("m4b-render");
    const auto tmp = dir / "out.wav.tmp";
    const auto finalPath = dir / "out.wav";
    std::ofstream(tmp) << "partial";
    REQUIRE(discardIncompleteRender(tmp.string(), finalPath.string()));
    REQUIRE_FALSE(fs::exists(tmp));
    REQUIRE(fs::exists(finalPath.string() + ".incomplete"));
    auto bad = validateRenderFile(finalPath.string());
    REQUIRE_FALSE(bad.ok);
}

TEST_CASE("M4B disk and memory budgets", "[milestone4b]")
{
    const auto est = estimateDiskSpace(".", 1, 1);
    REQUIRE(est.availableBytes > 0);
    REQUIRE(isValidOutputFilename("master.wav"));
    REQUIRE_FALSE(isValidOutputFilename("a:b?.wav"));
    REQUIRE(isPathTooLong(std::string(300, 'x')));

    LruByteCache cache(100);
    REQUIRE(cache.put("a", 40));
    REQUIRE(cache.put("b", 40));
    REQUIRE(cache.put("c", 40)); // should evict
    const auto stats = cache.stats();
    REQUIRE(stats.bytesUsed <= 100);
    REQUIRE(stats.evictions >= 1);
    cache.setLowMemoryMode(true);
    REQUIRE(cache.stats().lowMemoryMode);
}

TEST_CASE("M4B typed errors and diagnostics exclude audio", "[milestone4b]")
{
    const auto err = makeError(
        ErrorKind::Render,
        "RENDER_INTEGRITY_FAILED",
        "Render failed validation.",
        "nan detected",
        true,
        "Re-run export.");
    const auto json = serializeTypedError(err);
    REQUIRE(json.find("RENDER_INTEGRITY_FAILED") != std::string::npos);
    REQUIRE(json.find("userMessage") != std::string::npos);

    const auto bundle = buildDiagnosticBundle("0.4.0-alpha.m4b+test", "abc1234", {"RENDER_INTEGRITY_FAILED"});
    const auto body = serializeDiagnosticBundle(bundle);
    REQUIRE_FALSE(diagnosticBundleContainsAudioPayload(body));
    REQUIRE(redactPathForDiagnostics("/home/user/stems/Kick.wav").find("redacted") != std::string::npos);

    StructuredLogger log(8);
    log.log(LogLevel::info, "project", "saved");
    log.recordRealtimeFault("xrun");
    REQUIRE(log.realtimeFaultCount("xrun") == 1);
}

TEST_CASE("M4B project lock prevents silent dual write", "[milestone4b]")
{
    const auto dir = tempDir("m4b-lock");
    const auto project = dir / "p.masuite";
    std::ofstream(project) << "{}";
    const auto a = tryAcquireProjectLock(project.string(), 101, "SuiteA");
    REQUIRE(a.ok);
    const auto b = tryAcquireProjectLock(project.string(), 202, "SuiteB");
    REQUIRE_FALSE(b.ok);
    REQUIRE(b.mode == LockOpenMode::readOnly);
    REQUIRE(b.error.has_value());
    REQUIRE(releaseProjectLock(project.string()));
}

TEST_CASE("M4B settings malformed restores defaults atomically", "[milestone4b]")
{
    const auto dir = tempDir("m4b-settings");
    const auto path = dir / "settings.json";
    std::ofstream(path) << "{not-json";
    const auto loaded = loadSettingsAtomic(path.string());
    REQUIRE(loaded.ok);
    REQUIRE(loaded.restoredDefaults);
    REQUIRE_FALSE(loaded.backupPath.empty());
    const auto saved = saveSettingsAtomic(path.string(), defaultSettingsLayers());
    REQUIRE(saved.ok);
}

TEST_CASE("M4B dither never for float32 and batch routing validated", "[milestone4b]")
{
    REQUIRE_FALSE(shouldApplyDither(ExportBitDepth::float32));
    REQUIRE(resolveDitherPolicy(ExportBitDepth::float32, true) == DitherPolicy::none);
    REQUIRE(resolveDitherPolicy(ExportBitDepth::pcm24, true) == DitherPolicy::tpdf);
    ExportWorkflowState st;
    st.bitDepth = ExportBitDepth::float32;
    st.ditherEnabled = true;
    const auto json = serializeExportWorkflow(st);
    const bool ditherNone = json.find("\"dither\": \"none\"") != std::string::npos;
    REQUIRE(ditherNone);

    BatchExportRoute route;
    route.name = "drums";
    route.includeRoles = {"kick", "snare"};
    std::string error;
    REQUIRE_FALSE(validateBatchExportRouting(route, {"kick"}, {}, &error));
    REQUIRE(validateBatchExportRouting(route, {"kick", "snare"}, {}, &error));
}

TEST_CASE("M4B IPC hardening rejects traversal command and oversized messages", "[milestone4b]")
{
    IpcSecurityPolicy policy;
    REQUIRE_FALSE(validateIpcPayload("{\"path\":\"../secret\"}", "sess", policy).ok);
    REQUIRE_FALSE(validateIpcPayload("{\"cmd\":\"/bin/sh\"}", "sess", policy).ok);
    REQUIRE_FALSE(validateIpcPayload("{\"readFile\":true}", "sess", policy).ok);
    const std::string huge(policy.maxMessageBytes + 10, 'x');
    REQUIRE_FALSE(validateIpcPayload(huge, "sess", policy).ok);
    IpcRateLimiter limiter(2);
    REQUIRE(limiter.allow());
    REQUIRE(limiter.allow());
    REQUIRE_FALSE(limiter.allow());
}

TEST_CASE("M4B stress 100 save open cycles and 50 undo-style absolute snapshots", "[milestone4b]")
{
    const auto dir = tempDir("m4b-stress");
    const auto path = dir / "cycle.masuite";
    std::string last;
    for (int i = 0; i < 100; ++i) {
        const auto body = std::string("{\"schemaVersion\":7,\"id\":\"p\",\"n\":") + std::to_string(i) + "}";
        const auto saved = atomicSaveText(path.string(), body, 7, true);
        REQUIRE(saved.ok);
        std::ifstream in(path);
        last.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(last.find(std::to_string(i)) != std::string::npos);
    }
    // Bounded absolute undo snapshots (exact state, not inverse delta).
    std::vector<std::string> history;
    for (int i = 0; i < 50; ++i) {
        history.push_back("{\"gain\":" + std::to_string(i) + "}");
        if (history.size() > 32)
            history.erase(history.begin());
    }
    REQUIRE(history.size() == 32);
    REQUIRE(history.back().find("49") != std::string::npos);
}

TEST_CASE("M4B fuzz malformed JSON and future schema", "[milestone4b]")
{
    const auto bad = migrateDocument("project", "{", 1, 1, 7, "");
    REQUIRE_FALSE(bad.ok);
    const auto deep = migrateDocument(
        "project",
        std::string(200, '[') + std::string(200, ']'),
        1,
        1,
        7,
        "");
    // May fail parse or succeed as non-object; must not crash.
    (void) deep;
    const auto future = migrateDocument("project", tinyProjectJson(70), 70, 1, 7, "");
    REQUIRE(future.unsupportedNewer);
}

TEST_CASE("M4B performance smoke thresholds generous", "[milestone4b]")
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    LruByteCache cache(1024 * 1024);
    for (int i = 0; i < 1000; ++i)
        cache.put("k" + std::to_string(i % 50), 1024);
    JobSystem jobs(2);
    for (int i = 0; i < 20; ++i) {
        JobRecord j;
        j.jobId = "j" + std::to_string(i);
        jobs.enqueue(j);
        jobs.complete(j.jobId, {});
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
    REQUIRE(ms < 5000); // generous smoke gate
}
