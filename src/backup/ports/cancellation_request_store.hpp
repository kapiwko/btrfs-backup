// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <backup/model/backup_execution.hpp>

namespace btrfsbackup::backup {

class IActiveRunRegistration {
  public:
    virtual ~IActiveRunRegistration() = default;
    [[nodiscard]] virtual std::optional<std::string> close() = 0;
};

class ICancellationRequestStore {
  public:
    virtual ~ICancellationRequestStore() = default;

    [[nodiscard]] virtual std::unique_ptr<IActiveRunRegistration> register_active_run(
        const CancellationRequest& request
    ) = 0;
    [[nodiscard]] virtual CancellationRequestOutcome request_cancel(
        const CancellationRequest& request
    ) = 0;
    [[nodiscard]] virtual bool cancel_requested(const CancellationRequest& request) const = 0;
    virtual void clear_cancel_request(const CancellationRequest& request) = 0;
};

} // namespace btrfsbackup::backup
