// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <backup/model/pending_recovery.hpp>

namespace btrfsbackup {

class IPendingMarkerStore {
  public:
    virtual ~IPendingMarkerStore() = default;

    [[nodiscard]] virtual std::optional<PendingMarker> read(
        const std::filesystem::path& profile_state_dir,
        const std::string& source_id
    ) const = 0;
    virtual void write(
        const std::filesystem::path& profile_state_dir,
        const PendingMarker& marker
    ) = 0;
    virtual void clear(const std::filesystem::path& marker_path) = 0;
};

} // namespace btrfsbackup
