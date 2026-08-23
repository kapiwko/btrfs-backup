#pragma once

#include <filesystem>

#include <btrfsbackup/json.hpp>

namespace btrfsbackup {

Json profile_from_environment_sources(const std::filesystem::path& sources_table);

} // namespace btrfsbackup
