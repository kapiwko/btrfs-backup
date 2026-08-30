// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <string>

namespace btrfsbackup::platform::linux::process {

class ProcessEnvironment final {
  public:
    ProcessEnvironment();

    [[nodiscard]] static ProcessEnvironment for_btrfs_receive();
    [[nodiscard]] static ProcessEnvironment for_btrfs_send();
    [[nodiscard]] static ProcessEnvironment for_hook(
        const std::map<std::string, std::string>& allowed_variables
    );
    [[nodiscard]] static ProcessEnvironment for_systemd_control();

    [[nodiscard]] const std::map<std::string, std::string>& variables() const noexcept;

  private:
    explicit ProcessEnvironment(std::map<std::string, std::string> variables);

    std::map<std::string, std::string> variables_;
};

} // namespace btrfsbackup::platform::linux::process
