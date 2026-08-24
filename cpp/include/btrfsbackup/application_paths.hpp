#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

struct ApplicationPaths {
    std::filesystem::path sources_root;
    std::filesystem::path state_root;
    std::filesystem::path status_root;
    std::filesystem::path history_root;
};

inline std::filesystem::path profile_sources_dir(const ApplicationPaths& paths, const std::string& profile_id) {
    return paths.sources_root / profile_id / "sources.d";
}

inline std::filesystem::path profile_state_dir(const ApplicationPaths& paths, const std::string& profile_id) {
    return paths.state_root / "profiles" / profile_id;
}

} // namespace btrfsbackup
