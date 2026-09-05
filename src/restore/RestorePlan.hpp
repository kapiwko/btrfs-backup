// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <restore/RepositoryCatalog.hpp>

namespace btrfsbackup::restore {

enum class RestoreKind {
    Files,
    Subvolume,
    Drill,
};

enum class ExistingDestinationPolicy {
    Fail,
    Replace,
};

struct RestoreRequest {
    std::string transaction_id;
    std::string snapshot_id;
    RelativeRestorePath source_path{"unset"};
    std::filesystem::path destination;
    RestoreKind kind = RestoreKind::Files;
    ExistingDestinationPolicy existing_destination = ExistingDestinationPolicy::Fail;
};

struct RestorePlan {
    std::string transaction_id;
    std::string snapshot_id;
    std::string snapshot_uuid;
    std::filesystem::path source;
    std::filesystem::path destination;
    std::filesystem::path staging;
    std::filesystem::path previous;
    RestoreKind kind = RestoreKind::Files;
    ExistingDestinationPolicy existing_destination = ExistingDestinationPolicy::Fail;
    bool destination_exists = false;
};

class RestorePlanner {
  public:
    [[nodiscard]] RestorePlan plan(const RepositoryCatalog& catalog, const RestoreRequest& request) const;
    [[nodiscard]] RestorePlan plan_from_pinned_source(
        const RepositoryCatalog& catalog,
        const RestoreRequest& request,
        const std::filesystem::path& source
    ) const;
};

} // namespace btrfsbackup::restore
