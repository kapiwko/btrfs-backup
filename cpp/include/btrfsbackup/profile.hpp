#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <btrfsbackup/json.hpp>

namespace btrfsbackup {

std::string identifier(const Json& value, const std::string& name);
std::string env_get(const std::map<std::string, std::string>& env, const std::string& name, const std::string& default_value = "");
std::string env_required(const std::map<std::string, std::string>& env, const std::string& name);
bool env_bool(const std::map<std::string, std::string>& env, const std::string& name, bool default_value);
long long env_int(const std::map<std::string, std::string>& env, const std::string& name, long long default_value);

std::filesystem::path map_etc_path(const std::string& path, const std::filesystem::path& etc_root);
Json normalize_profile(const Json& raw);
Json load_profile_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);

} // namespace btrfsbackup
