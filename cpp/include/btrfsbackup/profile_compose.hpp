#pragma once

#include <filesystem>

#include <btrfsbackup/profile.hpp>

namespace btrfsbackup {

Profile profile_from_environment_sources(const std::filesystem::path& sources_table);

} // namespace btrfsbackup
