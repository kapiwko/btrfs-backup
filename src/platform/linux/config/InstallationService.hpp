// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <platform/linux/config/InstallationRender.hpp>

namespace btrfsbackup::platform::linux::config {

// Linux installation workflow exposed to frontends.

struct RenderInstallationRequest {
    std::filesystem::path profile_file;
    std::filesystem::path output_dir;
    InstallationRenderOptions options;
    std::filesystem::path target_mount_root = "/mnt/btrfs-backup";
};

void render_installation(const RenderInstallationRequest& request);
void validate_rendered_installation_at(
    const std::filesystem::path& root,
    const std::filesystem::path& target_mount_root = "/mnt/btrfs-backup"
);
void validate_active_installation_for(const std::string& profile_id);

} // namespace btrfsbackup::platform::linux::config
