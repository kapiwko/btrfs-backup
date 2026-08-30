// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/projection/StatusPublicationPolicy.hpp>

#include <cstdlib>

namespace btrfsbackup::state {

namespace {

constexpr std::uint64_t progress_publication_interval_ms = 1000;
constexpr int progress_publication_threshold = 5;

bool percentage_changed_by_threshold(std::optional<int> current, std::optional<int> published) {
    if (current.has_value() != published.has_value()) {
        return true;
    }
    return current.has_value() && std::abs(*current - *published) >= progress_publication_threshold;
}

} // namespace

bool StatusPublicationPolicy::should_publish(
    btrfsbackup::backup::BackupRunEventKind kind,
    const RunStatus& status,
    std::uint64_t elapsed_ms
) const {
    if (kind != btrfsbackup::backup::BackupRunEventKind::TransferProgress || !published_status_.has_value()) {
        return true;
    }

    const PublishedStatus& published = *published_status_;
    if (status.phase != published.phase || status.current_source_name != published.source_name || status.source_index != published.source_index) {
        return true;
    }
    if (percentage_changed_by_threshold(status.progress.source_percent, published.source_percent) ||
        percentage_changed_by_threshold(status.progress.overall_percent, published.overall_percent)) {
        return true;
    }
    if (!progress_elapsed_ms_.has_value() || elapsed_ms < *progress_elapsed_ms_) {
        return true;
    }
    return elapsed_ms - *progress_elapsed_ms_ >= progress_publication_interval_ms;
}

void StatusPublicationPolicy::record_publication(
    btrfsbackup::backup::BackupRunEventKind kind,
    const RunStatus& status,
    std::uint64_t elapsed_ms
) {
    published_status_ = PublishedStatus{
        .phase = status.phase,
        .source_name = status.current_source_name,
        .source_index = status.source_index,
        .source_percent = status.progress.source_percent,
        .overall_percent = status.progress.overall_percent,
    };
    if (kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress) {
        progress_elapsed_ms_ = elapsed_ms;
    }
}

void StatusPublicationPolicy::reset() {
    published_status_.reset();
    progress_elapsed_ms_.reset();
}

} // namespace btrfsbackup::state
