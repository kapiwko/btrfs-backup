// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/ConfigurationIdentity.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::config {

class IProfileRepository {
  public:
    virtual ~IProfileRepository() noexcept;

    [[nodiscard]] virtual LoadedProfile get(const ProfileId& profile_id) const = 0;
};

} // namespace btrfsbackup::config
