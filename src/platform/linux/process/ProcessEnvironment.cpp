// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/process/ProcessEnvironment.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::process {

namespace {

std::map<std::string, std::string> controlled_variables() {
    return {
        {"HOME", "/root"},
        {"LANG", "C.UTF-8"},
        {"LC_ALL", "C.UTF-8"},
        {"PATH", "/usr/bin"},
    };
}

void validate_variable(const std::string& name, const std::string& value) {
    if (name.empty() || name.contains('=') || name.contains('\0')) {
        throw ValidationError("invalid environment variable name");
    }
    if (value.contains('\0')) {
        throw ValidationError("invalid environment variable value for " + name);
    }
}

bool is_protected_variable(std::string_view name) {
    constexpr std::array protected_names{
        std::string_view{"BASH_ENV"},
        std::string_view{"ENV"},
        std::string_view{"GCONV_PATH"},
        std::string_view{"HOME"},
        std::string_view{"LANG"},
        std::string_view{"LC_ALL"},
        std::string_view{"PATH"},
        std::string_view{"PERL5LIB"},
        std::string_view{"PYTHONHOME"},
        std::string_view{"PYTHONPATH"},
        std::string_view{"RUBYLIB"},
    };
    if (std::ranges::find(protected_names, name) != protected_names.end()) {
        return true;
    }
    return name.starts_with("DYLD_") || name.starts_with("LD_");
}

} // namespace

ProcessEnvironment::ProcessEnvironment()
    : variables_(controlled_variables()) {
}

ProcessEnvironment::ProcessEnvironment(std::map<std::string, std::string> variables)
    : variables_(std::move(variables)) {
}

ProcessEnvironment ProcessEnvironment::for_btrfs_receive() {
    return {};
}

ProcessEnvironment ProcessEnvironment::for_btrfs_send() {
    return {};
}

ProcessEnvironment ProcessEnvironment::for_hook(
    const std::map<std::string, std::string>& allowed_variables
) {
    std::map<std::string, std::string> variables = controlled_variables();
    for (const auto& [name, value] : allowed_variables) {
        validate_variable(name, value);
        if (is_protected_variable(name)) {
            throw ValidationError("hook cannot override protected environment variable: " + name);
        }
        variables.emplace(name, value);
    }
    return ProcessEnvironment(std::move(variables));
}

ProcessEnvironment ProcessEnvironment::for_systemd_control() {
    std::map<std::string, std::string> variables = controlled_variables();
    variables["LC_ALL"] = "C";
    return ProcessEnvironment(std::move(variables));
}

const std::map<std::string, std::string>& ProcessEnvironment::variables() const noexcept {
    return variables_;
}

} // namespace btrfsbackup::platform::linux::process
