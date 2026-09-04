// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

struct CommandResult {
    int status{};
    std::string output;
    std::string error_output;
};

[[nodiscard]] CommandResult run_test_process(
    std::vector<std::string> arguments,
    std::chrono::seconds timeout,
    std::string_view standard_input = {}
);
[[nodiscard]] std::string command_diagnostic(const CommandResult& result);
[[nodiscard]] std::string trim_output(std::string value);
[[nodiscard]] std::string join_test_errors(const std::vector<std::string>& errors);
void write_test_file(const std::filesystem::path& path, std::string_view content);
void wipe_test_secret(std::string& secret) noexcept;

} // namespace btrfsbackup::integration
