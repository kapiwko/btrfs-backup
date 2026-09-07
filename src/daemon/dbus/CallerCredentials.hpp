// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>

#include <daemon/control/BrowseSessionService.hpp>

namespace btrfsbackup::daemon::dbus {

[[nodiscard]] std::uint32_t real_uid_from_credentials(sd_bus_creds* credentials);
[[nodiscard]] std::uint32_t effective_uid_from_credentials(sd_bus_creds* credentials);
[[nodiscard]] control::BrowseAccessIdentity filesystem_access_identity_from_credentials(
    sd_bus_creds* credentials
);

} // namespace btrfsbackup::daemon::dbus
