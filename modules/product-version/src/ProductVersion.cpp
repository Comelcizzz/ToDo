#include "mastering/product/ProductVersion.h"

#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>

#ifndef MASTERING_AUDIO_GIT_SHA
#define MASTERING_AUDIO_GIT_SHA "unknown"
#endif

namespace mastering::product {
namespace {

std::mutex gShaMutex;
std::string gOverrideSha;

} // namespace

std::string ProductVersion::semanticCore() const
{
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::string ProductVersion::full() const
{
    std::ostringstream oss;
    oss << semanticCore();
    if (!prerelease.empty())
        oss << '-' << prerelease;
    if (!shortCommit.empty() && shortCommit != "unknown")
        oss << '+' << shortCommit;
    return oss.str();
}

std::string ProductVersion::display() const
{
    return full();
}

std::string ProductVersion::toJson() const
{
    nlohmann::json j;
    j["major"] = major;
    j["minor"] = minor;
    j["patch"] = patch;
    j["prerelease"] = prerelease;
    j["shortCommit"] = shortCommit;
    j["full"] = full();
    j["profileSchemaVersion"] = profileSchemaVersion;
    j["engineRevision"] = engineRevision;
    j["projectSchemaVersion"] = projectSchemaVersion;
    return j.dump(2);
}

ProductVersion currentProductVersion() noexcept
{
    ProductVersion v;
    {
        std::lock_guard lock(gShaMutex);
        v.shortCommit = gOverrideSha.empty() ? std::string {MASTERING_AUDIO_GIT_SHA} : gOverrideSha;
    }
    if (v.shortCommit.size() > 7)
        v.shortCommit = v.shortCommit.substr(0, 7);
    return v;
}

void setBuildCommitSha(std::string shortSha) noexcept
{
    std::lock_guard lock(gShaMutex);
    if (shortSha.size() > 7)
        shortSha.resize(7);
    gOverrideSha = std::move(shortSha);
}

} // namespace mastering::product
