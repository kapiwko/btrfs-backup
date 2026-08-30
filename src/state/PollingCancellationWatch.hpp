// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <thread>

#include <backup/ports/CancellationMonitor.hpp>
#include <backup/ports/CancellationRequestStore.hpp>

namespace btrfsbackup::state {

class PollingCancellationWatch final : public btrfsbackup::backup::ICancellationWatch {
  public:
    PollingCancellationWatch(
        btrfsbackup::backup::ICancellationRequestStore& requests,
        btrfsbackup::backup::CancellationRequest request,
        CancellationToken& cancellation
    );
    PollingCancellationWatch(const PollingCancellationWatch&) = delete;
    PollingCancellationWatch& operator=(const PollingCancellationWatch&) = delete;
    ~PollingCancellationWatch() override;

    std::optional<std::string> close() override;

  private:
    void run(std::stop_token stop);

    btrfsbackup::backup::ICancellationRequestStore& requests_;
    btrfsbackup::backup::CancellationRequest request_;
    CancellationToken& cancellation_;
    std::jthread worker_;
    bool closed_ = false;
};

} // namespace btrfsbackup::state
