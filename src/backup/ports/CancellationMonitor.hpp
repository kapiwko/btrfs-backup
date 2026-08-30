// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <backup/model/BackupExecution.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {

class ICancellationWatch {
  public:
    virtual ~ICancellationWatch() = default;
    [[nodiscard]] virtual std::optional<std::string> close() = 0;
};

class ICancellationMonitor {
  public:
    virtual ~ICancellationMonitor() = default;

    [[nodiscard]] virtual std::unique_ptr<ICancellationWatch> watch(
        const CancellationRequest& request,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup::backup
