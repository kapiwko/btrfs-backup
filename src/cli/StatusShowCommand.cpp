// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/StatusShowCommand.hpp>

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <core/Errors.hpp>
#include <config/model/Json.hpp>
#include <state/StatusService.hpp>

namespace fs = std::filesystem;
using json = btrfsbackup::config::Json;

namespace {

std::string string_or_empty(const json& data, const char* key) {
    auto it = data.find(key);
    if (it == data.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

void print_json_document(const btrfsbackup::state::StatusDocument& document, std::ostream& output) {
    output << document.content;
    if (document.content.empty() || document.content.back() != '\n') {
        output << '\n';
    }
}

void print_human_status(const btrfsbackup::state::StatusDocument& document, std::ostream& output) {
    const json& data = document.data;
    std::string profile = string_or_empty(data, "profileName");
    if (profile.empty()) {
        profile = string_or_empty(data, "profileId");
    }
    if (profile.empty()) {
        profile = document.source.parent_path().filename().string();
    }
    std::string state = string_or_empty(data, "state");
    output << (profile.empty() ? "unknown" : profile) << ": " << (state.empty() ? "unknown" : state) << '\n';

    const std::vector<std::pair<const char*, const char*>> fields = {
        {"phase", "  phase: "},
        {"message", "  "},
        {"sourceName", "  source: "},
        {"currentSourceName", "  source: "},
        {"targetName", "  target: "},
        {"updatedAt", "  updated: "},
        {"errorCode", "  error code: "},
        {"errorMessage", "  error: "},
    };
    for (const auto& [key, prefix] : fields) {
        std::string value = string_or_empty(data, key);
        if (!value.empty()) {
            output << prefix << value << '\n';
        }
    }
}

} // namespace

namespace btrfsbackup::cli {

void status_show(
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

    for (const btrfsbackup::state::StatusDocument& document : btrfsbackup::state::get_statuses(status_root, history_root, profile, all)) {
        if (human)
            print_human_status(document, output);
        else
            print_json_document(document, output);
    }
}

} // namespace btrfsbackup::cli
