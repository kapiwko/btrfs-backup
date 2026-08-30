// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <config/ApplicationConfig.hpp>
#include <config/domain/Profile.hpp>
#include <config/ports/IProfileRepository.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::platform::linux {

// Filesystem-backed Linux adapter for profile configuration.

std::filesystem::path profile_json_path(const std::filesystem::path& etc_root, const std::string& profile_id);
btrfsbackup::config::Profile load_profile_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);

using ProfileFileReader = std::function<std::string(const std::filesystem::path&)>;

class FileProfileRepository final : public btrfsbackup::config::IProfileRepository {
  public:
    explicit FileProfileRepository(std::filesystem::path config_root);
    FileProfileRepository(std::filesystem::path config_root, btrfsbackup::config::ApplicationConfig application_config);
    FileProfileRepository(
        std::filesystem::path config_root,
        btrfsbackup::config::ApplicationConfig application_config,
        ProfileFileReader profile_reader
    );

    [[nodiscard]] btrfsbackup::config::LoadedProfile get(const ProfileId& profile_id) const override;

  private:
    std::filesystem::path config_root_;
    btrfsbackup::config::ApplicationConfig application_config_;
    ProfileFileReader profile_reader_;
};

} // namespace btrfsbackup::platform::linux
