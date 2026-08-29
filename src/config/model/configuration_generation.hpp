// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>

namespace btrfsbackup::config {

class ConfigurationGeneration {
  public:
    explicit ConfigurationGeneration(std::string value) : value_(std::move(value)) {
    }

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return value_.empty();
    }

    bool operator==(const ConfigurationGeneration&) const = default;

  private:
    std::string value_;
};

} // namespace btrfsbackup::config
