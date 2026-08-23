#include <btrfsbackup/status.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>

namespace fs = std::filesystem;
using json = btrfsbackup::Json;

namespace {

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool readable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && std::ifstream(path).good();
}

std::string string_or_empty(const json& data, const char* key) {
    auto it = data.find(key);
    if (it == data.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

void print_json_file(const fs::path& path, std::ostream& output) {
    std::string content = read_text_file(path);
    output << content;
    if (content.empty() || content.back() != '\n') {
        output << '\n';
    }
}

void print_human_status(const fs::path& path, std::ostream& output) {
    json data = btrfsbackup::load_json_file(path);
    std::string profile = string_or_empty(data, "profileName");
    if (profile.empty()) {
        profile = string_or_empty(data, "profileId");
    }
    std::string state = string_or_empty(data, "state");
    output << (profile.empty() ? "unknown" : profile) << ": " << (state.empty() ? "unknown" : state) << '\n';

    const std::vector<std::pair<const char*, const char*>> fields = {
        {"phase", "  phase: "},
        {"message", "  "},
        {"currentSourceName", "  source: "},
        {"updatedAt", "  updated: "},
        {"error", "  error: "},
    };
    for (const auto& [key, prefix] : fields) {
        std::string value = string_or_empty(data, key);
        if (!value.empty()) {
            output << prefix << value << '\n';
        }
    }
}

} // namespace

namespace btrfsbackup {

void command_status(
    const fs::path& status_root,
    const fs::path& history_root,
    const std::vector<std::string>& args,
    std::ostream& output
) {
    std::string profile = "default";
    bool all = false;
    bool human = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--all") {
            all = true;
        } else if (arg == "--human") {
            human = true;
        } else {
            throw ValidationError("unknown status option: " + arg);
        }
    }

    if (all) {
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(status_root, ec) && !ec) {
            for (const auto& profile_entry : fs::directory_iterator(status_root, ec)) {
                if (ec) {
                    break;
                }
                fs::path current = profile_entry.path() / "current.json";
                if (readable_file(current)) {
                    files.push_back(current);
                }
            }
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) {
            throw ValidationError("no status files found under " + status_root.string());
        }
        for (const auto& path : files) {
            if (human) {
                print_human_status(path, output);
            } else {
                print_json_file(path, output);
            }
        }
        return;
    }

    validate_profile_id(profile);
    fs::path path = status_root / profile / "current.json";
    if (!readable_file(path) && readable_file(history_root / profile / "last.json")) {
        path = history_root / profile / "last.json";
    }
    if (human) {
        if (!readable_file(path)) {
            throw ValidationError("cannot read " + path.string());
        }
        print_human_status(path, output);
    } else {
        print_json_file(path, output);
    }
}

} // namespace btrfsbackup
