// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/model/json.hpp>

namespace btrfsbackup::platform::linux {

void validate_legacy_profile_runtime_fields(
    const btrfsbackup::config::Json& raw,
    const std::filesystem::path& target_mount_root
);

} // namespace btrfsbackup::platform::linux
