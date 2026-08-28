// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/profile_query_service.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <core/errors.hpp>
#include <core/identifiers.hpp>
#include <daemon/manager_document_reader.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon {

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
        btrfsbackup::config::Json profile = read_manager_json_document(file);
        if (!profile.is_object() || profile.value("schemaVersion", 0) != 1) {
            throw ValidationError("public profile has an unsupported schema: " + file.string());
        }
        const std::string profile_id = profile.value("profileId", "");
        validate_profile_id(profile_id);
        if (!profile.contains("sources") || !profile.at("sources").is_array()) {
            throw ValidationError("public profile has invalid sources: " + file.string());
        }
        std::vector<ProfileSourceSummary> sources;
        sources.reserve(profile.at("sources").size());
        for (const btrfsbackup::config::Json& source : profile.at("sources")) {
            sources.push_back({
                .id = source.value("id", std::string{}),
                .name = source.value("name", std::string{}),
            });
        }
        result.push_back(ProfileSummary{
            .profile_id = profile_id,
            .name = profile.value("name", std::string{}),
            .target_name = profile.value("target", btrfsbackup::config::Json::object()).value("name", std::string{}),
            .sources = std::move(sources),
        });
    }
    return result;
}

} // namespace btrfsbackup::daemon
