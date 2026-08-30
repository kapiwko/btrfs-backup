// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <backup/ports/Process.hpp>

namespace btrfsbackup::platform::linux {

class ControlledCommandSession final {
  public:
    ControlledCommandSession(
        const std::vector<std::string>& argv,
        const btrfsbackup::backup::ControlledCommandOptions& options
    );

    [[nodiscard]] btrfsbackup::backup::CommandResult run();

  private:
    const std::vector<std::string>& argv_;
    const btrfsbackup::backup::ControlledCommandOptions& options_;
};

} // namespace btrfsbackup::platform::linux
