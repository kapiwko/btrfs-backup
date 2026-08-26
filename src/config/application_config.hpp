// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/application_paths.hpp>

namespace btrfsbackup {

class ApplicationConfig {
public:
    ApplicationConfig();
    explicit ApplicationConfig(ApplicationPaths paths);

    static ApplicationConfig defaults();
    static ApplicationConfig load(const std::filesystem::path& config_root = "/etc/btrfs-backup");

    const ApplicationPaths& paths() const;

private:
    ApplicationPaths paths_;
};

} // namespace btrfsbackup
