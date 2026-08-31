// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileRuntimePolicy.hpp>
#include <config/domain/Validation.hpp>
#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::config {

void validate_profile_runtime_policy(const btrfsbackup::config::Profile& profile) {
    const auto validate_hooks = [](const std::vector<btrfsbackup::config::ProfileHookCommand>& hooks) {
        for (const btrfsbackup::config::ProfileHookCommand& hook : hooks) {
            const fs::path program = fs::path(hook.program).lexically_normal();
            if (program.parent_path() != fs::path(trusted_hook_directory) || program.filename().empty() ||
                program.filename() == "." || program.filename() == "..") {
                throw ValidationError(
                    "hook program must be a direct child of " + std::string(trusted_hook_directory)
                );
            }
        }
    };
    validate_hooks(profile.hooks.before_snapshot);
    validate_hooks(profile.hooks.after_snapshot);
}

} // namespace btrfsbackup::platform::linux::config
