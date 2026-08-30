// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace btrfsbackup::cli::profile {

int profile_wizard(const std::vector<std::string>& args);

} // namespace btrfsbackup::cli::profile
