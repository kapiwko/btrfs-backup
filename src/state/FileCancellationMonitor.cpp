// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/FileCancellationMonitor.hpp>

#include <state/PollingCancellationWatch.hpp>

namespace btrfsbackup::state {

FileCancellationMonitor::FileCancellationMonitor(
    btrfsbackup::backup::ICancellationRequestStore& requests
)
    : requests_(requests) {
}

std::unique_ptr<btrfsbackup::backup::ICancellationWatch> FileCancellationMonitor::watch(
    const btrfsbackup::backup::CancellationRequest& request,
    CancellationToken& cancellation
) {
    return std::make_unique<PollingCancellationWatch>(requests_, request, cancellation);
}

} // namespace btrfsbackup::state
