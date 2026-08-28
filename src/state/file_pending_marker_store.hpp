// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/pending_marker_store.hpp>

namespace btrfsbackup {
class IDurableFileOperations;
}

namespace btrfsbackup::state {

class FilePendingMarkerStore final : public btrfsbackup::backup::IPendingMarkerStore {
  public:
    explicit FilePendingMarkerStore(IDurableFileOperations& files);

    [[nodiscard]] std::optional<btrfsbackup::backup::PendingMarker> read(
        const std::filesystem::path& profile_state_dir,
        const std::string& source_id
    ) const override;
    void write(
        const std::filesystem::path& profile_state_dir,
        const btrfsbackup::backup::PendingMarker& marker
    ) override;
    void clear(const std::filesystem::path& marker_path) override;

  private:
    IDurableFileOperations& files_;
};

} // namespace btrfsbackup::state
