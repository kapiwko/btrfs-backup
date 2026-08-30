// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/LinkedCancellationMonitor.hpp>

#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
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
          callback_(std::in_place, upstream.stop_token(), [&cancellation] { cancellation.request_cancel(); }) {
    }

    LinkedCancellationWatch(const LinkedCancellationWatch&) = delete;
    LinkedCancellationWatch& operator=(const LinkedCancellationWatch&) = delete;
    LinkedCancellationWatch(LinkedCancellationWatch&&) = delete;
    LinkedCancellationWatch& operator=(LinkedCancellationWatch&&) = delete;

    ~LinkedCancellationWatch() noexcept override {
        if (const auto& diagnostic = close()) {
            std::clog << "btrfs-backup: linked cancellation watch cleanup failed: " << diagnostic->message << '\n';
        }
    }

    const std::optional<CleanupDiagnostic>& close() noexcept override {
        if (closed_) {
            return close_diagnostic_;
        }
        closed_ = true;
        callback_.reset();
        if (const auto& diagnostic = primary_->close()) {
            close_diagnostic_ = *diagnostic;
        }
        primary_.reset();
        return close_diagnostic_;
    }

  private:
    std::unique_ptr<ICancellationWatch> primary_;
    std::optional<std::stop_callback<std::function<void()>>> callback_;
    bool closed_ = false;
    std::optional<CleanupDiagnostic> close_diagnostic_;
};

static_assert(std::is_nothrow_destructible_v<LinkedCancellationWatch>);

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
