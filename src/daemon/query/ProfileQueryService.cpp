// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/query/ProfileQueryService.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/query/ManagerDocumentReader.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::query {

ProfileQueryService::ProfileQueryService(fs::path public_profile_root)
    : public_profile_root_(std::move(public_profile_root)) {
}

std::vector<ProfileSummary> ProfileQueryService::list_profiles() const {
    std::error_code error;
    if (!fs::is_directory(public_profile_root_, error) || error) {
        return {};
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(public_profile_root_, error)) {
        if (error) {
            break;
        }
        if (entry.path().extension() == ".json" && manager_regular_file_without_symlink(entry)) {
            files.push_back(entry.path());
        }
    }
    if (error) {
        throw ValidationError("cannot enumerate public profiles");
    }
    std::sort(files.begin(), files.end());

    std::vector<ProfileSummary> result;
    result.reserve(files.size());
    for (const fs::path& file : files) {
        btrfsbackup::config::json::Json profile = read_manager_json_document(file);
        if (!profile.is_object())
            throw ValidationError("public profile is not an object: " + file.string());
        const std::string profile_id = profile.value("profileId", "");
        validate_profile_id(profile_id);
        if (profile.value("schemaVersion", 0) != 1) {
            result.push_back(ProfileSummary{
                .profile_id = profile_id,
                .name = profile.value("name", profile_id),
                .enabled = false,
                .target_name = {},
                .sources = {},
                .configuration_valid = false,
                .configuration_error_code = "configuration.unsupported_profile_schema",
            });
            continue;
        }
        if (!profile.contains("sources") || !profile.at("sources").is_array()) {
            throw ValidationError("public profile has invalid sources: " + file.string());
        }
        std::vector<ProfileSourceSummary> sources;
        sources.reserve(profile.at("sources").size());
        for (const btrfsbackup::config::json::Json& source : profile.at("sources")) {
            sources.push_back({
                .id = source.value("id", std::string{}),
                .name = source.value("name", std::string{}),
            });
        }
        result.push_back(ProfileSummary{
            .profile_id = profile_id,
            .name = profile.value("name", std::string{}),
            .enabled = profile.value("enabled", true),
            .target_name = profile.value("target", btrfsbackup::config::json::Json::object()).value("name", std::string{}),
            .sources = std::move(sources),
            .configuration_valid = true,
            .configuration_error_code = {},
        });
    }
    return result;
}

} // namespace btrfsbackup::daemon::query
