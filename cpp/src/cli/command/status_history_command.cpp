#include <btrfsbackup/cli/command/status_history_command.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <regex>
#include <string>
#include <vector>

#include <btrfsbackup/model/errors.hpp>
#include <btrfsbackup/model/identifiers.hpp>

namespace fs = std::filesystem;

namespace {

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<fs::path> sorted_files(const fs::path& directory, const std::string& suffix, bool reverse = false) {
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
    if (reverse) {
        std::reverse(files.begin(), files.end());
    }
    return files;
}

} // namespace

namespace btrfsbackup::command {

void status_history(const fs::path& history_root, const std::vector<std::string>& args, std::ostream& output) {
    std::string profile = "default";
    int limit = 50;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--limit" && i + 1 < args.size()) {
            std::string value = args[++i];
            if (!std::regex_match(value, std::regex("^[0-9]+$"))) {
                throw ValidationError("--limit must be a number");
            }
            limit = std::stoi(value);
        } else {
            throw ValidationError("unknown history option: " + arg);
        }
    }

    validate_profile_id(profile);
    fs::path directory = history_root / profile;
    std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec) {
        output << "[]\n";
        return;
    }

    std::vector<fs::path> files;
    for (const auto& path : sorted_files(directory, ".json", true)) {
        if (path.filename() != "last.json") {
            files.push_back(path);
        }
    }

    output << "[\n";
    int count = 0;
    for (const auto& path : files) {
        if (count >= limit) {
            break;
        }
        if (count > 0) {
            output << ",\n";
        }
        output << read_text_file(path);
        ++count;
    }
    output << "\n]\n";
}

} // namespace btrfsbackup::command
