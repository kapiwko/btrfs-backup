// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace btrfsbackup::state {
namespace document {

class BoundedDocumentReader {
  public:
    [[nodiscard]] std::string read(
        const std::filesystem::path& path,
        std::size_t maximum_size,
        std::optional<std::uint32_t> expected_owner = std::nullopt,
        std::optional<std::uint32_t> expected_permissions = std::nullopt
    ) const;

  private:
    [[nodiscard]] static int open_without_symlinks(const std::filesystem::path& path);
};

} // namespace document
} // namespace btrfsbackup::state
