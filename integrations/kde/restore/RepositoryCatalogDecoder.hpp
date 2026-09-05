// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <filesystem>

#include <restore/RepositoryCatalog.hpp>

namespace btrfsbackup::kde::restore {

class RepositoryCatalogDecoder final {
  public:
    [[nodiscard]] btrfsbackup::restore::RepositoryCatalog decode(
        const QString& payload,
        const std::filesystem::path& root_path
    ) const;
};

} // namespace btrfsbackup::kde::restore
