#pragma once

#include <filesystem>

namespace btrfsbackup::wizard {

std::filesystem::path default_output_dir();
void assert_safe_output_dir(const std::filesystem::path& output_dir);

} // namespace btrfsbackup::wizard
