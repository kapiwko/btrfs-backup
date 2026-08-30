// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <restore/RepositoryCatalog.hpp>

namespace btrfsbackup::restore {

enum class SnapshotEntryType {
    File,
    Directory,
    Symlink,
    Other,
};

struct SnapshotEntry {
    std::string name;
    SnapshotEntryType type = SnapshotEntryType::Other;
    std::uintmax_t size = 0;
};

class SnapshotBrowser {
  public:
    [[nodiscard]] std::vector<SnapshotEntry> list(
        const RepositoryCatalog& catalog,
        const std::string& snapshot_id,
        const RelativeRestorePath& relative_path
    ) const;

    [[nodiscard]] std::filesystem::path resolve_regular_file(
        const RepositoryCatalog& catalog,
        const std::string& snapshot_id,
        const RelativeRestorePath& relative_path
    ) const;
};

} // namespace btrfsbackup::restore
