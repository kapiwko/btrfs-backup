// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <core/cancellation.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup {

class ICancellationWatch {
  public:
    virtual ~ICancellationWatch() = default;
};

class ICancellationMonitor {
  public:
    virtual ~ICancellationMonitor() = default;

    [[nodiscard]] virtual std::unique_ptr<ICancellationWatch> watch(
        const ProfileId& profile_id,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup
