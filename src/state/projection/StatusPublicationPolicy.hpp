// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <backup/model/BackupRunEvent.hpp>
#include <state/model/RunStatus.hpp>

namespace btrfsbackup::state {

class StatusPublicationPolicy {
  public:
    [[nodiscard]] bool should_publish(
        btrfsbackup::backup::BackupRunEventKind kind,
        const RunStatus& status,
        std::uint64_t elapsed_ms
    ) const;
    void record_publication(
        btrfsbackup::backup::BackupRunEventKind kind,
        const RunStatus& status,
        std::uint64_t elapsed_ms
    );
    void reset();

  private:
    struct PublishedStatus {
        RunPhase phase;
        std::string source_name;
        int source_index = 0;
        std::optional<int> source_percent;
        std::optional<int> overall_percent;
    };

    std::optional<PublishedStatus> published_status_;
    std::optional<std::uint64_t> progress_elapsed_ms_;
};

} // namespace btrfsbackup::state
