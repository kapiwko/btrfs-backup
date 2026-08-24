#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

void validate_rendered_installation(const std::filesystem::path& root);
void validate_active_installation(const std::string& profile_id);

} // namespace btrfsbackup
