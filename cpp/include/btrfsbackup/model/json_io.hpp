#pragma once

#include <filesystem>
#include <string>

#include <btrfsbackup/model/json.hpp>

namespace btrfsbackup {

Json load_json_file(const std::filesystem::path& path);
std::string dump_json(const Json& data);

} // namespace btrfsbackup
