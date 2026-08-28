// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::platform::linux {

[[nodiscard]] std::string systemd_mount_unit_name(const std::filesystem::path& mount_point);

} // namespace btrfsbackup::platform::linux
