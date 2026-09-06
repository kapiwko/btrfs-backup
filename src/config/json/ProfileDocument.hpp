// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/json/Json.hpp>
#include <config/domain/Profile.hpp>

namespace btrfsbackup::config::json {

inline constexpr int current_profile_schema_version = 1;

struct ProfileDocument {
    Json value;
};

Json normalize_profile(const Json& raw, const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup");
ProfileDocument normalize_profile_document(
    const Json& raw,
    const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup"
);
Profile profile_from_document(
    const ProfileDocument& document,
    const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup"
);
Profile profile_from_json(const Json& raw, const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup");
ProfileDocument profile_to_document(const Profile& profile);
Json profile_to_json(const Profile& profile);

} // namespace btrfsbackup::config::json
