// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <core/RuntimeTime.hpp>

namespace btrfsbackup::restore {

inline constexpr int repository_format_version = 1;
inline constexpr int catalog_format_version = 1;

class RelativeRestorePath {
  public:
    explicit RelativeRestorePath(std::string value);

    [[nodiscard]] const std::filesystem::path& value() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    auto operator<=>(const RelativeRestorePath&) const = default;

  private:
    std::filesystem::path value_;
};

struct RepositoryIdentity {
    std::string repository_id;
    std::string target_filesystem_uuid;
    RuntimeTimePoint created_at;
    std::vector<std::string> features;
};

struct CatalogSnapshot {
    std::string snapshot_id;
    std::string host_id;
    std::string profile_id;
    std::string source_id;
    RelativeRestorePath repository_path{"unset"};
    RuntimeTimePoint created_at;
    std::string uuid;
    std::string received_uuid;
    std::string parent_uuid;
    bool verified = false;
};

class RepositoryCatalog {
  public:
    RepositoryCatalog(
        std::filesystem::path root,
        RepositoryIdentity identity,
        std::uint64_t generation,
        std::vector<CatalogSnapshot> snapshots
    );

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] const RepositoryIdentity& identity() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] const std::vector<CatalogSnapshot>& snapshots() const noexcept;
    [[nodiscard]] const CatalogSnapshot& snapshot(const std::string& snapshot_id) const;

  private:
    std::filesystem::path root_;
    RepositoryIdentity identity_;
    std::uint64_t generation_ = 0;
    std::vector<CatalogSnapshot> snapshots_;
};

[[nodiscard]] std::vector<const CatalogSnapshot*> find_versions(
    const RepositoryCatalog& catalog,
    const std::string& host_id,
    const std::string& profile_id,
    const std::string& source_id,
    const RelativeRestorePath& relative_path
);

} // namespace btrfsbackup::restore
