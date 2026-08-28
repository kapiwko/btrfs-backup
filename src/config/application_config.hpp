// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/application_paths.hpp>

namespace btrfsbackup::config {

class ApplicationConfig {
  public:
    ApplicationConfig();
    explicit ApplicationConfig(ApplicationPaths paths);

    static ApplicationConfig defaults();
    const ApplicationPaths& paths() const;

  private:
    ApplicationPaths paths_;
};

} // namespace btrfsbackup::config
