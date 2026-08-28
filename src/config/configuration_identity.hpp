// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>

#include <config/model/profile.hpp>

namespace btrfsbackup::config {

class ConfigurationFingerprint {
  public:
    explicit ConfigurationFingerprint(std::string value) : value_(std::move(value)) {
    }

    [[nodiscard]] const std::string& value() const {
        return value_;
    }

  private:
    std::string value_;
};

class ConfigurationGeneration {
  public:
    explicit ConfigurationGeneration(std::string value) : value_(std::move(value)) {
    }

    [[nodiscard]] const std::string& value() const {
        return value_;
    }

  private:
    std::string value_;
};

struct LoadedProfile {
    Profile profile;
    ConfigurationFingerprint fingerprint;
    ConfigurationGeneration generation;
};

} // namespace btrfsbackup::config
