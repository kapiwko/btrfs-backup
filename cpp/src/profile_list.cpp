#include <btrfsbackup/profile_list.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> sorted_files(const fs::path& directory, const std::string& suffix) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec) {
        return files;
    }
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<fs::path> sorted_profile_json_files(const fs::path& directory) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec) {
        return files;
    }
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory(ec) || ec) {
            ec.clear();
            continue;
        }
        fs::path profile_json = entry.path() / "profile.json";
        if (fs::is_regular_file(profile_json, ec) && !ec) {
            files.push_back(profile_json);
        }
        ec.clear();
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

namespace btrfsbackup {

void command_list_profiles(
    const fs::path& profile_config_dir,
    const fs::path& profile_root_dir,
    std::ostream& output
) {
    std::set<std::string> profiles;
    for (const auto& file : sorted_profile_json_files(profile_root_dir)) {
        std::string profile = file.parent_path().filename().string();
        validate_profile_id(profile);
        profiles.insert(profile);
    }

    std::vector<fs::path> files = sorted_files(profile_config_dir, ".env");
    for (const auto& file : files) {
        std::string profile = file.stem().string();
        validate_profile_id(profile);
        profiles.insert(profile);
    }

    for (const std::string& profile : profiles) {
        output << profile << '\n';
    }

    if (profiles.empty()) {
        throw ValidationError("no profiles found");
    }
}

} // namespace btrfsbackup
