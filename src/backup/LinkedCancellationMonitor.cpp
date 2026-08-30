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

    ~LinkedCancellationWatch() override {
        try {
            if (std::optional<std::string> diagnostic = close()) {
                std::clog << "btrfs-backup: linked cancellation watch cleanup failed: " << *diagnostic << '\n';
            }
        } catch (const std::exception& error) {
            std::clog << "btrfs-backup: linked cancellation watch cleanup failed: " << error.what() << '\n';
        } catch (...) {
            std::clog << "btrfs-backup: linked cancellation watch cleanup failed with an unknown error\n";
        }
    }

    std::optional<std::string> close() override {
        if (closed_) {
            return std::nullopt;
        }
        closed_ = true;
        callback_.reset();
        std::optional<std::string> result = primary_->close();
        primary_.reset();
        return result;
    }

  private:
    std::unique_ptr<ICancellationWatch> primary_;
    std::optional<std::stop_callback<std::function<void()>>> callback_;
    bool closed_ = false;
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
