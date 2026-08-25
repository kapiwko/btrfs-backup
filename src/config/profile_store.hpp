// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <config/errors.hpp>
#include <config/profile.hpp>

namespace btrfsbackup {

struct RollbackError {
    std::string operation;
    std::filesystem::path path;
    std::string message;
};

struct RollbackResult {
    bool complete = true;
    std::vector<RollbackError> errors;
};

struct ConfigurationSaveError : CodedValidationError {
    ConfigurationSaveError(std::string message, RollbackResult rollback_result);

    RollbackResult rollback_result;
};

void render_tree(const Profile& profile, const std::filesystem::path& output_dir);
void save_tree(
    const Profile& profile,
    const std::filesystem::path& etc_root,
    const std::filesystem::path& udev_root,
    const std::filesystem::path& systemd_root,
    const std::filesystem::path& public_root,
    const std::function<void()>& activate = {}
);

} // namespace btrfsbackup
