#include "mastering/reliability/PortablePackage.h"

#include "mastering/analysis/Sha256.h"
#include "mastering/reliability/AtomicFile.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace mastering::reliability {

bool isSafePackageRelativePath(std::string_view relativePath) noexcept
{
    if (relativePath.empty())
        return false;
    if (relativePath.find("..") != std::string_view::npos)
        return false;
    if (!relativePath.empty() && (relativePath.front() == '/' || relativePath.front() == '\\'))
        return false;
    if (relativePath.size() >= 2 && relativePath[1] == ':')
        return false;
    return true;
}

PortablePackageResult exportPortablePackage(const PortablePackageRequest& request)
{
    PortablePackageResult result;
    try {
        if (request.includeStems || request.includeReferences) {
            if (!request.explicitAudioConsent) {
                result.error = makeError(
                    ErrorKind::Export,
                    "PACKAGE_AUDIO_CONSENT_REQUIRED",
                    "Copyrighted audio is not included without explicit confirmation.",
                    "includeStems/references requested without consent",
                    true,
                    "Confirm audio inclusion or export metadata-only.");
                return result;
            }
        }

        const fs::path root = fs::path(request.destinationDirectory) / "package";
        fs::create_directories(root);
        fs::create_directories(root / "reports");
        fs::create_directories(root / "profiles");

        const auto projectOut = root / "project.masuite";
        const auto saved = atomicSaveText(projectOut.string(), request.projectJson, 7, false);
        if (!saved.ok) {
            result.error = saved.error;
            return result;
        }
        result.copiedFiles.push_back(projectOut.string());

        nlohmann::json manifest;
        manifest["schemaVersion"] = 1;
        manifest["mode"] = request.mode == PackageMode::metadataOnly ? "metadata-only" : "full-local";
        manifest["includesAudio"] =
            (request.includeStems || request.includeReferences) && request.explicitAudioConsent;
        manifest["files"] = nlohmann::json::array();

        auto copySafe = [&](std::string_view src, std::string_view rel) -> bool {
            if (!isSafePackageRelativePath(rel)) {
                result.error = makeError(
                    ErrorKind::Export,
                    "PACKAGE_PATH_TRAVERSAL",
                    "Package path traversal was blocked.",
                    std::string(rel),
                    false,
                    "Use relative paths inside the package folder only.");
                return false;
            }
            const fs::path dest = root / std::string(rel);
            if (!dest.string().starts_with(root.string())) {
                result.error = makeError(
                    ErrorKind::Export,
                    "PACKAGE_PATH_ESCAPE",
                    "Resolved package path escaped destination directory.",
                    dest.string(),
                    false,
                    "Choose another destination.");
                return false;
            }
            fs::create_directories(dest.parent_path());
            std::error_code ec;
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
            if (ec)
                return false;
            result.copiedFiles.push_back(dest.string());
            manifest["files"].push_back({{"relative", rel}, {"sha256", analysis::sha256FileHex(dest.string())}});
            return true;
        };

        if (request.mode == PackageMode::fullLocal && request.explicitAudioConsent) {
            int i = 0;
            if (request.includeStems) {
                for (const auto& stem : request.stemPaths) {
                    const auto rel = "stems/stem_" + std::to_string(i++) + ".wav";
                    if (!copySafe(stem, rel))
                        return result;
                }
            }
            i = 0;
            if (request.includeReferences) {
                for (const auto& ref : request.referencePaths) {
                    const auto rel = "references/ref_" + std::to_string(i++) + ".wav";
                    if (!copySafe(ref, rel))
                        return result;
                }
            }
        }

        const auto manPath = root / "package-manifest.json";
        const auto manBody = manifest.dump(2);
        (void) atomicSaveText(manPath.string(), manBody, 1, false);
        result.manifestPath = manPath.string();
        result.checksumSha256 = analysis::sha256Hex(manBody);
        result.packageRoot = root.string();
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = makeError(
            ErrorKind::Export,
            "PACKAGE_EXPORT_FAILED",
            "Portable package export failed.",
            ex.what(),
            true,
            "Retry with a writable destination folder.");
        return result;
    }
}

PortablePackageResult validatePortablePackage(std::string_view packageRoot)
{
    PortablePackageResult result;
    result.packageRoot = std::string(packageRoot);
    const fs::path root(packageRoot);
    const auto man = root / "package-manifest.json";
    const auto project = root / "project.masuite";
    if (!fs::exists(man) || !fs::exists(project)) {
        result.error = makeError(
            ErrorKind::Export,
            "PACKAGE_INVALID",
            "Package is missing required files.",
            std::string(packageRoot),
            false,
            "Re-export the package.");
        return result;
    }
    result.manifestPath = man.string();
    result.ok = true;
    return result;
}

PortablePackageResult importPortablePackage(
    std::string_view packageRoot,
    std::string_view restoreDirectory)
{
    auto validated = validatePortablePackage(packageRoot);
    if (!validated.ok)
        return validated;
    PortablePackageResult result;
    try {
        const fs::path dest(restoreDirectory);
        fs::create_directories(dest);
        std::error_code ec;
        fs::copy(packageRoot, dest / "imported", fs::copy_options::recursive, ec);
        if (ec) {
            result.error = makeError(
                ErrorKind::Export,
                "PACKAGE_IMPORT_FAILED",
                "Could not restore portable package.",
                ec.message(),
                true,
                "Choose a writable restore folder.");
            return result;
        }
        result.packageRoot = (dest / "imported").string();
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = makeError(
            ErrorKind::Export,
            "PACKAGE_IMPORT_EXCEPTION",
            "Package import failed.",
            ex.what(),
            true,
            "Retry import.");
        return result;
    }
}

} // namespace mastering::reliability
