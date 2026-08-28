// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/ports/configuration_activator.hpp>

namespace btrfsbackup::platform::linux {

class LinuxSystemConfigurationActivator final : public btrfsbackup::config::IConfigurationActivator {
  public:
    void activate() override;
};

} // namespace btrfsbackup::platform::linux
