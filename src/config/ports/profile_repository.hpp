// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <config/application_paths.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::config {

class IProfileRepository {
  public:
    virtual ~IProfileRepository();

    [[nodiscard]] virtual Profile get(const ProfileId& profile_id) const = 0;
    [[nodiscard]] virtual const ApplicationPaths& application_paths() const = 0;
    [[nodiscard]] virtual std::string fingerprint(const Profile& profile) const = 0;
};

} // namespace btrfsbackup::config
