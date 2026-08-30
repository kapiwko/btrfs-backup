// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace btrfsbackup::platform::linux::systemd {

[[nodiscard]] std::string systemd_mount_unit_name(const std::filesystem::path& mount_point);
[[nodiscard]] std::string systemd_cryptsetup_unit_name(const std::string& mapper_name);
[[nodiscard]] std::string target_activation_unit_name(std::string_view profile_id);
[[nodiscard]] std::optional<std::filesystem::path> locate_systemd_unit_file(
    std::string_view unit_name,
    std::span<const std::filesystem::path> unit_roots
);
[[nodiscard]] std::optional<std::filesystem::path> locate_systemd_unit_file(
    std::string_view unit_name
);

} // namespace btrfsbackup::platform::linux::systemd
