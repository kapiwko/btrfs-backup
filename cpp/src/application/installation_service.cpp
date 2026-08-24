#include <btrfsbackup/application/installation_service.hpp>

#include <btrfsbackup/application/installation_validate.hpp>
#include <btrfsbackup/application/profile_service.hpp>

namespace btrfsbackup {

void render_installation(const RenderInstallationRequest& request) {
    render_installation_files(validate_profile_file(request.profile_file), request.output_dir, request.options);
}

void validate_rendered_installation_at(const std::filesystem::path& root) {
    validate_rendered_installation(root);
}

void validate_active_installation_for(const std::string& profile_id) {
    validate_active_installation(profile_id);
}

} // namespace btrfsbackup
