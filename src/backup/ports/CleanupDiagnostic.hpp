// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace btrfsbackup::backup {

struct CleanupDiagnostic {
    std::string message;
};

} // namespace btrfsbackup::backup
