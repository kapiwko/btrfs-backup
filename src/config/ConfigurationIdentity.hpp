// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>

#include <config/domain/ConfigurationGeneration.hpp>
#include <config/domain/Profile.hpp>

namespace btrfsbackup::config {

inline constexpr const char* expected_configuration_generation_environment = "BTRFS_BACKUP_CONFIGURATION_GENERATION";
inline constexpr const char* expected_configuration_fingerprint_environment = "BTRFS_BACKUP_CONFIGURATION_FINGERPRINT";
inline constexpr const char* authorized_operation_id_environment = "BTRFS_BACKUP_OPERATION_ID";
inline constexpr int configuration_changed_exit_code = 78;

class ConfigurationFingerprint {
  public:
    explicit ConfigurationFingerprint(std::string value) : value_(std::move(value)) {
    }

    [[nodiscard]] const std::string& value() const {
        return value_;
    }

    bool operator==(const ConfigurationFingerprint&) const = default;

  private:
    std::string value_;
};

struct LoadedProfile {
    Profile profile;
    ConfigurationFingerprint fingerprint;
    ConfigurationGeneration generation;
};

} // namespace btrfsbackup::config
