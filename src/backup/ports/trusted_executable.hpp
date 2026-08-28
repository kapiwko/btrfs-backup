// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace btrfsbackup::backup {

struct TrustedExecutablePolicy {
    bool allow_current_user_owner = false;
    bool verify_parent_directories = true;
};

class ITrustedExecutable {
  public:
    virtual ~ITrustedExecutable() = default;

    [[nodiscard]] virtual std::string execution_path() const = 0;
    [[nodiscard]] virtual std::vector<int> inherited_fds() const = 0;
};

class ITrustedExecutableResolver {
  public:
    virtual ~ITrustedExecutableResolver() = default;

    [[nodiscard]] virtual std::unique_ptr<ITrustedExecutable> resolve(
        const std::filesystem::path& program
    ) const = 0;
};

} // namespace btrfsbackup::backup
