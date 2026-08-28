// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

class ICancellationRequestStore {
  public:
    virtual ~ICancellationRequestStore() = default;

    virtual void request_cancel(const ProfileId& profile_id) = 0;
    [[nodiscard]] virtual bool cancel_requested(const ProfileId& profile_id) const = 0;
    virtual void clear_cancel_request(const ProfileId& profile_id) = 0;
};

} // namespace btrfsbackup::backup
