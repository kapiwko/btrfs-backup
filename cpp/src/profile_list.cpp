#include <btrfsbackup/profile_list.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
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

} // namespace

namespace btrfsbackup {

void command_list_profiles(
    const fs::path& profile_config_dir,
    const fs::path& legacy_config_file,
    std::ostream& output
) {
    bool found = false;
    std::vector<fs::path> files = sorted_files(profile_config_dir, ".env");
    for (const auto& file : files) {
        std::string profile = file.stem().string();
        validate_profile_id(profile);
        output << profile << '\n';
        found = true;
    }

    std::error_code ec;
    bool has_legacy = fs::is_regular_file(legacy_config_file, ec) && !ec;
    ec.clear();
    bool has_default_profile = fs::exists(profile_config_dir / "default.env", ec) && !ec;
    if (has_legacy && !has_default_profile) {
        output << "default (legacy)\n";
        found = true;
    }
    if (!found) {
        throw ValidationError("no profiles found");
    }
}

} // namespace btrfsbackup
