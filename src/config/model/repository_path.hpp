// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <compare>
#include <filesystem>
#include <string>

namespace btrfsbackup::config {

class RemoteSnapshotRoot {
  public:
    explicit RemoteSnapshotRoot(std::string value);

    [[nodiscard]] const std::filesystem::path& value() const noexcept;

    auto operator<=>(const RemoteSnapshotRoot&) const = default;

  private:
    std::filesystem::path value_;
};

class IncomingRoot {
  public:
    explicit IncomingRoot(std::string value);

    [[nodiscard]] const std::filesystem::path& value() const noexcept;

    auto operator<=>(const IncomingRoot&) const = default;

  private:
    std::filesystem::path value_;
};

class SafeRelativePath {
  public:
    explicit SafeRelativePath(std::string value);

    [[nodiscard]] const std::filesystem::path& value() const noexcept;

    auto operator<=>(const SafeRelativePath&) const = default;

  private:
    std::filesystem::path value_;
};

} // namespace btrfsbackup::config
