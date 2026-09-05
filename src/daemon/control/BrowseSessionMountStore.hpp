// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <core/Identifiers.hpp>

namespace btrfsbackup::daemon::control {

struct BrowseSessionMountRecord {
    std::string target_key;
    std::string target_unit;
    std::filesystem::path directory;
    std::filesystem::path view;
    std::filesystem::path marker;
    std::uint32_t caller_uid = 0;
    bool view_mounted = false;
    bool target_mounted_by_backend = false;
    bool target_released = false;
};

class BrowseSessionMountStore final {
  public:
    explicit BrowseSessionMountStore(
        std::filesystem::path session_root,
        std::uint32_t trusted_uid = 0
    );

    void prepare_root() const;
    [[nodiscard]] BrowseSessionMountRecord make_record(
        const BrowseSessionId& session_id,
        std::uint32_t caller_uid,
        std::string target_key,
        std::string target_unit,
        bool target_mounted_by_backend
    ) const;
    void write(const BrowseSessionId& session_id, const BrowseSessionMountRecord& record) const;
    [[nodiscard]] std::optional<BrowseSessionMountRecord> read(
        const std::filesystem::path& marker
    ) const;
    [[nodiscard]] std::vector<std::pair<BrowseSessionId, BrowseSessionMountRecord>> stale_records(
        const std::set<std::string>& live_session_ids
    ) const;
    void remove_session_directory(const BrowseSessionMountRecord& record) const;
    void remove_marker(const BrowseSessionMountRecord& record) const;

    [[nodiscard]] const std::filesystem::path& root() const noexcept;

  private:
    void require_root_directory(const std::filesystem::path& path) const;
    void require_private_directory(
        const std::filesystem::path& path,
        std::filesystem::perms mode
    ) const;

    std::filesystem::path session_root_;
    std::uint32_t trusted_uid_;
};

} // namespace btrfsbackup::daemon::control
