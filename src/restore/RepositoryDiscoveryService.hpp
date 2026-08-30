// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <restore/RepositoryCatalog.hpp>

namespace btrfsbackup::restore {

struct DiscoveredSnapshotMetadata {
    bool is_subvolume = false;
    bool readonly = false;
    std::string uuid;
    std::string received_uuid;
};

using DiscoveredSnapshotMetadataReader = std::function<std::optional<DiscoveredSnapshotMetadata>(
    const std::filesystem::path&
)>;

class RepositoryDiscoveryService {
  public:
    explicit RepositoryDiscoveryService(DiscoveredSnapshotMetadataReader metadata_reader);

    [[nodiscard]] RepositoryCatalog discover(const std::filesystem::path& already_mounted_root) const;

  private:
    DiscoveredSnapshotMetadataReader metadata_reader_;
};

} // namespace btrfsbackup::restore
