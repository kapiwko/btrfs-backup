// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/installation_service.hpp>

#include <config/installation_validate.hpp>
#include <config/profile_service.hpp>

namespace btrfsbackup {

void render_installation(const RenderInstallationRequest& request) {
    render_installation_files(
        validate_profile_file(request.profile_file, request.target_mount_root),
        request.output_dir,
        request.options
    );
}

void validate_rendered_installation_at(const std::filesystem::path& root, const std::filesystem::path& target_mount_root) {
    validate_rendered_installation(root, target_mount_root);
}

void validate_active_installation_for(const std::string& profile_id) {
    validate_active_installation(profile_id);
}

} // namespace btrfsbackup
