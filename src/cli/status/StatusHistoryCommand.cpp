// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/status/StatusHistoryCommand.hpp>

#include <filesystem>
#include <ostream>
#include <regex>
#include <string>
#include <vector>

#include <core/Errors.hpp>
#include <state/StatusService.hpp>
#include <state/RunHistory.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::cli::status {

void status_history(const fs::path& history_root, const std::vector<std::string>& args, std::ostream& output) {
    std::string profile = "default";
    int limit = 50;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--limit" && i + 1 < args.size()) {
            const std::string& value = args[++i];
            if (!std::regex_match(value, std::regex("^[0-9]+$"))) {
                throw ValidationError("--limit must be a number");
            }
            limit = std::stoi(value);
        } else {
            throw ValidationError("unknown history option: " + arg);
        }
    }

    std::vector<btrfsbackup::state::StatusDocument> documents = btrfsbackup::state::get_status_history(history_root, profile, static_cast<std::size_t>(limit));
    if (documents.empty()) {
        output << "[]\n";
        return;
    }
    output << "[\n";
    for (std::size_t i = 0; i < documents.size(); ++i) {
        if (i > 0) {
            output << ",\n";
        }
        output << documents[i].content;
    }
    output << "\n]\n";
}

} // namespace btrfsbackup::cli::status
