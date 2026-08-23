#pragma once

#include <filesystem>
#include <iosfwd>

namespace btrfsbackup {

enum class ProfileWizardAction {
    render,
    apply,
    validate_active,
    validate_rendered
};

struct ProfileWizardOptions {
    ProfileWizardAction action = ProfileWizardAction::render;
    std::filesystem::path output_dir;
    std::filesystem::path validate_dir;
    std::string profile_id = "default";
};

int run_profile_wizard(const ProfileWizardOptions& options, std::istream& input, std::ostream& output);

} // namespace btrfsbackup
