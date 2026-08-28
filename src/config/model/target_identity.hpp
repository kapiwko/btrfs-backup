// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <compare>
#include <string>

namespace btrfsbackup::config {

class LuksUuid {
  public:
    explicit LuksUuid(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

    auto operator<=>(const LuksUuid&) const = default;

  private:
    std::string value_;
};

class BtrfsUuid {
  public:
    explicit BtrfsUuid(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

    auto operator<=>(const BtrfsUuid&) const = default;

  private:
    std::string value_;
};

class PartitionUuid {
  public:
    explicit PartitionUuid(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    auto operator<=>(const PartitionUuid&) const = default;

  private:
    std::string value_;
};

class MapperName {
  public:
    explicit MapperName(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

    auto operator<=>(const MapperName&) const = default;

  private:
    std::string value_;
};

} // namespace btrfsbackup::config
