#pragma once

#include <filesystem>
#include <string>

#include <btrfsbackup/application/installation_render.hpp>

namespace btrfsbackup {

struct RenderInstallationRequest {
    std::filesystem::path profile_file;
    std::filesystem::path output_dir;
    InstallationRenderOptions options;
};

void render_installation(const RenderInstallationRequest& request);
void validate_rendered_installation_at(const std::filesystem::path& root);
void validate_active_installation_for(const std::string& profile_id);

} // namespace btrfsbackup
