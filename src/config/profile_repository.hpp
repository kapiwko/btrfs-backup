// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/application_config.hpp>
#include <config/model/json.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup {

std::filesystem::path profile_json_path(const std::filesystem::path& etc_root, const std::string& profile_id);
Json load_profile_json_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);
Profile load_profile_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);

class IProfileRepository {
  public:
    virtual ~IProfileRepository() = default;
    [[nodiscard]] virtual Profile get(const ProfileId& profile_id) const = 0;
    [[nodiscard]] virtual const ApplicationPaths& application_paths() const = 0;
    [[nodiscard]] virtual std::string fingerprint(const Profile& profile) const = 0;
};

class FileProfileRepository final : public IProfileRepository {
  public:
    explicit FileProfileRepository(std::filesystem::path config_root);
    FileProfileRepository(std::filesystem::path config_root, ApplicationConfig application_config);

    [[nodiscard]] Profile get(const ProfileId& profile_id) const override;
    [[nodiscard]] const ApplicationPaths& application_paths() const override;
    [[nodiscard]] std::string fingerprint(const Profile& profile) const override;

  private:
    std::filesystem::path config_root_;
    ApplicationConfig application_config_;
};

} // namespace btrfsbackup
