// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <backup/ports/IMountInspector.hpp>
#include <core/Identifiers.hpp>
#include <daemon/control/SourceCandidate.hpp>

namespace btrfsbackup::daemon::control {

[[nodiscard]] std::vector<SourceCandidate> provisioning_source_candidates(
    const std::vector<backup::MountEntry>& mounts
);

[[nodiscard]] SourceCandidate resolve_provisioning_source(
    const std::vector<backup::MountEntry>& mounts,
    const std::filesystem::path& source,
    const ProfileId& profile_id
);

} // namespace btrfsbackup::daemon::control
