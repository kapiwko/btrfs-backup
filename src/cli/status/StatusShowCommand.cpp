// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/status/StatusShowCommand.hpp>

#include <filesystem>
#include <ostream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <core/Errors.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>
#include <state/query/StatusService.hpp>

namespace fs = std::filesystem;

namespace {

void print_json_document(const btrfsbackup::state::StatusDocument& document, std::ostream& output) {
    output << document.content;
    if (document.content.empty() || document.content.back() != '\n') {
        output << '\n';
    }
}

void print_human_status(const btrfsbackup::state::StatusDocument& document, std::ostream& output) {
    std::visit(
        [&](const auto& status) {
            using Status = std::decay_t<decltype(status)>;
            if constexpr (std::is_same_v<Status, btrfsbackup::state::document::PublicRunStatusV1>) {
                const std::string profile = document.source.parent_path().filename().string();
                output << (profile.empty() ? "unknown" : profile) << ": "
                       << btrfsbackup::state::document::public_run_state_name(status) << '\n';
                if (!status.phase.value.empty())
                    output << "  phase: " << status.phase.value << '\n';
                if (!status.source_name.empty())
                    output << "  source: " << status.source_name << '\n';
                if (!status.target_name.empty())
                    output << "  target: " << status.target_name << '\n';
                const std::string error = btrfsbackup::state::document::public_error_code_name(status.error_code);
                if (!error.empty())
                    output << "  error code: " << error << '\n';
            } else {
                const std::string profile = status.profile_name.empty()
                    ? std::string(status.profile_id.value())
                    : status.profile_name;
                output << profile << ": " << status.state.value << '\n';
                if (!status.phase.value.empty())
                    output << "  phase: " << status.phase.value << '\n';
                if (!status.message.empty())
                    output << "  " << status.message << '\n';
                if (!status.current_source_name.empty())
                    output << "  source: " << status.current_source_name << '\n';
                if (!status.target_name.empty())
                    output << "  target: " << status.target_name << '\n';
                if (!status.updated_at.empty())
                    output << "  updated: " << status.updated_at << '\n';
                if (!status.error_code.empty())
                    output << "  error code: " << status.error_code << '\n';
                if (!status.error_message.empty())
                    output << "  error: " << status.error_message << '\n';
            }
        },
        document.status
    );
}

} // namespace

namespace btrfsbackup::cli::status {

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

} // namespace btrfsbackup::cli::status
