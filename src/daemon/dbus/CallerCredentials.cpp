// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/CallerCredentials.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace btrfsbackup::daemon::dbus {
namespace {

template <typename Id>
[[nodiscard]] std::uint32_t checked_id(Id id, const char* description) {
    if (id > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error(std::string(description) + " is outside the supported range");
    return static_cast<std::uint32_t>(id);
}

[[nodiscard]] std::runtime_error credential_error(const char* description, int result) {
    return std::runtime_error(std::string("cannot resolve D-Bus caller ") + description + ": " + std::strerror(-result));
}

} // namespace

std::uint32_t real_uid_from_credentials(sd_bus_creds* credentials) {
    uid_t uid = 0;
    const int result = sd_bus_creds_get_uid(credentials, &uid);
    if (result < 0)
        throw credential_error("real UID", result);
    return checked_id(uid, "D-Bus caller real UID");
}

std::uint32_t effective_uid_from_credentials(sd_bus_creds* credentials) {
    uid_t uid = 0;
    const int result = sd_bus_creds_get_euid(credentials, &uid);
    if (result < 0)
        throw credential_error("effective UID", result);
    return checked_id(uid, "D-Bus caller effective UID");
}

control::BrowseAccessIdentity filesystem_access_identity_from_credentials(sd_bus_creds* credentials) {
    uid_t uid = 0;
    int uid_result = sd_bus_creds_get_fsuid(credentials, &uid);
    if (uid_result == -ENODATA)
        uid_result = sd_bus_creds_get_euid(credentials, &uid);
    if (uid_result < 0)
        throw credential_error("filesystem UID", uid_result);

    gid_t gid = 0;
    int gid_result = sd_bus_creds_get_fsgid(credentials, &gid);
    if (gid_result == -ENODATA)
        gid_result = sd_bus_creds_get_egid(credentials, &gid);
    if (gid_result < 0)
        throw credential_error("filesystem GID", gid_result);

    control::BrowseAccessIdentity identity{
        .uid = checked_id(uid, "D-Bus caller filesystem UID"),
        .groups = {checked_id(gid, "D-Bus caller filesystem GID")},
    };
    const gid_t* supplementary = nullptr;
    const int group_count = sd_bus_creds_get_supplementary_gids(credentials, &supplementary);
    if (group_count < 0 && group_count != -ENODATA)
        throw credential_error("supplementary groups", group_count);
    for (int index = 0; index < std::max(group_count, 0); ++index) {
        identity.groups.push_back(checked_id(
            supplementary[index],
            "D-Bus caller supplementary GID"
        ));
    }
    std::ranges::sort(identity.groups);
    identity.groups.erase(std::ranges::unique(identity.groups).begin(), identity.groups.end());
    return identity;
}

} // namespace btrfsbackup::daemon::dbus
