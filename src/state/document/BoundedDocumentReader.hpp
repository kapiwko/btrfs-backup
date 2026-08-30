// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace btrfsbackup::state {
namespace document {

class BoundedDocumentReader {
  public:
    [[nodiscard]] std::string read(
        const std::filesystem::path& path,
        std::size_t maximum_size
    ) const;
};

} // namespace document
} // namespace btrfsbackup::state
