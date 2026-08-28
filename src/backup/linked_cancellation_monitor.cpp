// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/linked_cancellation_monitor.hpp>

#include <functional>
#include <memory>
#include <stop_token>
#include <utility>

namespace btrfsbackup::backup {

namespace {

class LinkedCancellationWatch final : public ICancellationWatch {
  public:
    LinkedCancellationWatch(
        std::unique_ptr<ICancellationWatch> primary,
        CancellationToken& upstream,
        CancellationToken& cancellation
    )
        : primary_(std::move(primary)),
          callback_(upstream.stop_token(), [&cancellation] { cancellation.request_cancel(); }) {
    }

  private:
    std::unique_ptr<ICancellationWatch> primary_;
    std::stop_callback<std::function<void()>> callback_;
};

} // namespace

LinkedCancellationMonitor::LinkedCancellationMonitor(
    ICancellationMonitor& primary,
    CancellationToken& upstream
)
    : primary_(primary), upstream_(upstream) {
}

std::unique_ptr<ICancellationWatch> LinkedCancellationMonitor::watch(
    const CancellationRequest& request,
    CancellationToken& cancellation
) {
    return std::make_unique<LinkedCancellationWatch>(
        primary_.watch(request, cancellation),
        upstream_,
        cancellation
    );
}

} // namespace btrfsbackup::backup
