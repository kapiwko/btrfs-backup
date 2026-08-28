// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/application_config.hpp>
#include <config/model/profile.hpp>
#include <config/model/profile_document.hpp>
#include <config/ports/profile_repository.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::platform::linux {

// Filesystem-backed Linux adapter for profile configuration.

std::filesystem::path profile_json_path(const std::filesystem::path& etc_root, const std::string& profile_id);
btrfsbackup::config::ProfileDocument load_profile_document_by_id(
    const std::filesystem::path& etc_root,
    const std::string& profile_id
);
btrfsbackup::config::Profile load_profile_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);

class FileProfileRepository final : public btrfsbackup::config::IProfileRepository {
  public:
    explicit FileProfileRepository(std::filesystem::path config_root);
    FileProfileRepository(std::filesystem::path config_root, btrfsbackup::config::ApplicationConfig application_config);

    [[nodiscard]] btrfsbackup::config::Profile get(const ProfileId& profile_id) const override;
    [[nodiscard]] const btrfsbackup::config::ApplicationPaths& application_paths() const override;
    [[nodiscard]] std::string fingerprint(const btrfsbackup::config::Profile& profile) const override;

  private:
    std::filesystem::path config_root_;
    btrfsbackup::config::ApplicationConfig application_config_;
};

} // namespace btrfsbackup::platform::linux
