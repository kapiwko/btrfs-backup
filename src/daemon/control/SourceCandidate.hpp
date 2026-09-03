// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace btrfsbackup::daemon::control {

struct SourceCandidate {
    std::string id;
    std::string path;
    std::string filesystem_uuid;
    std::string mount_root;
    std::string local_snapshot_root;
};

} // namespace btrfsbackup::daemon::control
