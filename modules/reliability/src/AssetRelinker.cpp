#include "mastering/reliability/AssetRelinker.h"

#include "mastering/analysis/Sha256.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace mastering::reliability {
namespace {

std::string fileFingerprint(const fs::path& path)
{
    return analysis::sha256FileHex(path.string());
}

bool metadataCompatible(const AssetIdentity& asset, const fs::path& path)
{
    // Without decoding audio here, accept existing file size as weak check when
    // fingerprint matches. Duration/SR/channels validated by caller when known.
    (void) asset;
    return fs::exists(path) && fs::is_regular_file(path);
}

} // namespace

RelinkResult verifyAsset(const AssetIdentity& asset)
{
    RelinkResult r;
    const fs::path path(asset.expectedPath);
    if (!fs::exists(path)) {
        r.status = RelinkStatus::missing;
        r.message = "Asset file is missing";
        r.error = makeError(
            ErrorKind::Asset,
            "ASSET_MISSING",
            "A project stem or reference is missing.",
            asset.expectedPath,
            true,
            "Relink by fingerprint or choose a new file.");
        return r;
    }
    const auto fp = fileFingerprint(path);
    r.matchedFingerprint = fp;
    if (!asset.contentFingerprint.empty() && fp != asset.contentFingerprint) {
        r.status = RelinkStatus::fingerprintMismatch;
        r.message = "Content fingerprint mismatch";
        r.analysisCacheInvalidated = true;
        r.error = makeError(
            ErrorKind::Asset,
            "ASSET_FINGERPRINT_MISMATCH",
            "The file at this path no longer matches the project fingerprint.",
            asset.expectedPath,
            true,
            "Relink to the correct asset. Analysis cache was invalidated.");
        return r;
    }
    r.status = RelinkStatus::ok;
    r.resolvedPath = path.string();
    r.message = "Asset verified";
    return r;
}

RelinkResult findByFingerprint(const AssetIdentity& asset, const RelinkSearchOptions& options)
{
    RelinkResult direct = verifyAsset(asset);
    if (direct.status == RelinkStatus::ok)
        return direct;

    std::vector<fs::path> roots;
    if (!options.projectFolder.empty())
        roots.emplace_back(options.projectFolder);
    if (!options.userSelectedFolder.empty())
        roots.emplace_back(options.userSelectedFolder);

    for (const auto& root : roots) {
        if (!fs::exists(root))
            continue;
        for (const auto& entry : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file())
                continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".WAV")
                continue;
            // Never accept name-only match by default.
            if (options.allowNameOnlyMatch
                && entry.path().filename() == fs::path(asset.expectedPath).filename()
                && asset.contentFingerprint.empty()) {
                RelinkResult rejected;
                rejected.status = RelinkStatus::nameOnlyMatchRejected;
                rejected.message = "Name-only match rejected without fingerprint";
                rejected.error = makeError(
                    ErrorKind::Asset,
                    "ASSET_NAME_ONLY_REJECTED",
                    "A file with the same name was found, but fingerprint verification is required.",
                    entry.path().string(),
                    true,
                    "Confirm fingerprint match or choose the file manually.");
                return rejected;
            }
            if (asset.contentFingerprint.empty())
                continue;
            const auto fp = fileFingerprint(entry.path());
            if (fp == asset.contentFingerprint && metadataCompatible(asset, entry.path())) {
                RelinkResult hit;
                hit.status = RelinkStatus::relinked;
                hit.resolvedPath = entry.path().string();
                hit.matchedFingerprint = fp;
                hit.analysisCacheInvalidated = false;
                hit.message = "Relinked by content fingerprint";
                return hit;
            }
        }
    }

    RelinkResult miss;
    miss.status = RelinkStatus::missing;
    miss.message = "No fingerprint match found";
    miss.error = makeError(
        ErrorKind::Asset,
        "ASSET_RELINK_NOT_FOUND",
        "Could not find a matching audio file by fingerprint.",
        asset.assetId,
        true,
        "Select a folder to search or relink manually.");
    return miss;
}

RelinkResult manualRelink(const AssetIdentity& asset, std::string_view newPath)
{
    AssetIdentity copy = asset;
    copy.expectedPath = std::string(newPath);
    auto verified = verifyAsset(copy);
    if (verified.status == RelinkStatus::ok) {
        if (!asset.contentFingerprint.empty()
            && verified.matchedFingerprint != asset.contentFingerprint) {
            verified.status = RelinkStatus::fingerprintMismatch;
            verified.analysisCacheInvalidated = true;
            verified.message = "Manual relink path has different content; cache invalidated";
        } else {
            verified.status = RelinkStatus::relinked;
            verified.message = "Manual relink accepted";
        }
    }
    return verified;
}

std::string serializeRelinkResult(const RelinkResult& result)
{
    nlohmann::json j;
    j["status"] = static_cast<int>(result.status);
    j["resolvedPath"] = result.resolvedPath;
    j["matchedFingerprint"] = result.matchedFingerprint;
    j["analysisCacheInvalidated"] = result.analysisCacheInvalidated;
    j["message"] = result.message;
    if (result.error)
        j["error"] = nlohmann::json::parse(serializeTypedError(*result.error));
    return j.dump(2);
}

} // namespace mastering::reliability
