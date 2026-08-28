// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/model/json.hpp>

namespace btrfsbackup::daemon {

class ProfileQueryService {
  public:
    explicit ProfileQueryService(std::filesystem::path public_profile_root);

    [[nodiscard]] btrfsbackup::config::Json list_profiles() const;

  private:
    std::filesystem::path public_profile_root_;
};

} // namespace btrfsbackup::daemon
