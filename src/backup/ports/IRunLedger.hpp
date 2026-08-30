// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include <config/model/Profile.hpp>
#include <config/ConfigurationIdentity.hpp>
#include <core/Identifiers.hpp>
#include <core/RuntimeTime.hpp>

namespace btrfsbackup::backup {

class IRunLedger {
  public:
    virtual ~IRunLedger() = default;

    [[nodiscard]] virtual bool last_success_matches(
        const btrfsbackup::config::Profile& profile,
        LocalDate date,
        const btrfsbackup::config::ConfigurationFingerprint& fingerprint
    ) const = 0;
    virtual void write_skipped(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        RuntimeTimePoint started_at,
        RuntimeTimePoint finished_at,
        std::size_t source_count
    ) = 0;
    virtual void write_success(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        LocalDate date,
        RuntimeTimePoint timestamp,
        const btrfsbackup::config::ConfigurationFingerprint& fingerprint,
        std::size_t source_count
    ) = 0;
};

} // namespace btrfsbackup::backup
