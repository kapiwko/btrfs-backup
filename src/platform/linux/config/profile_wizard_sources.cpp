// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/profile_wizard_sources.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ostream>
#include <set>
#include <sstream>
#include <string>

#include <core/errors.hpp>
#include <platform/linux/mount_info.hpp>
#include <config/wizard/profile_wizard_prompt.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

std::string source_name_from_path(const std::string& path) {
    if (path == "/") {
        return "root";
    }
    std::string name = fs::path(path).filename().string();
    for (char& ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '_' && ch != '-') {
            ch = '-';
        }
    }
    if (name.empty() || !std::isalnum(static_cast<unsigned char>(name.front()))) {
        name = "source-" + name;
    }
    return name;
}

std::vector<std::string> detect_btrfs_sources() {
    return btrfs_mount_targets();
}

std::string default_source_selection(const std::vector<std::string>& candidates) {
    std::vector<std::string> defaults;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i] == "/" || candidates[i] == "/home") {
            defaults.push_back(std::to_string(i + 1));
        }
    }
    if (defaults.empty()) {
        return "1";
    }
    std::string result = defaults.front();
    for (std::size_t i = 1; i < defaults.size(); ++i) {
        result += "," + defaults[i];
    }
    return result;
}

std::vector<std::string> selected_sources_from_input(const std::vector<std::string>& candidates, const std::string& selection) {
    if (candidates.empty()) {
        throw ValidationError("no mounted Btrfs source subvolumes were detected");
    }
    std::string normalized = btrfsbackup::config::trim_text(selection);
    if (normalized == "a" || normalized == "A") {
        return candidates;
    }

    std::vector<std::string> selected;
    std::set<std::size_t> seen;
    std::istringstream tokens(normalized);
    std::string token;
    while (std::getline(tokens, token, ',')) {
        token = btrfsbackup::config::trim_text(token);
        if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); })) {
            throw ValidationError("invalid source selection: " + token);
        }
        std::size_t index = static_cast<std::size_t>(std::stoul(token));
        if (index == 0 || index > candidates.size()) {
            throw ValidationError("source selection out of range: " + token);
        }
        if (seen.insert(index - 1).second) {
            selected.push_back(candidates[index - 1]);
        }
    }
    if (selected.empty()) {
        throw ValidationError("no sources selected");
    }
    return selected;
}

std::vector<std::string> select_sources(std::istream& input, std::ostream& output) {
    std::vector<std::string> candidates = detect_btrfs_sources();
    if (candidates.empty()) {
        throw ValidationError("no mounted Btrfs source subvolumes were detected");
    }
    output << "\nDetected mounted Btrfs sources:\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        output << " " << (i + 1) << ") " << candidates[i] << '\n';
    }

    std::string selection = btrfsbackup::config::prompt_value(
        input,
        output,
        "Select one or more sources, comma-separated, or 'a' for all",
        default_source_selection(candidates)
    );
    return selected_sources_from_input(candidates, selection);
}

} // namespace btrfsbackup::platform::linux
