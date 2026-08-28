// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

class IRunLedger {
  public:
    virtual ~IRunLedger() = default;

    [[nodiscard]] virtual bool last_success_matches(
        const btrfsbackup::config::Profile& profile,
        const std::string& date,
        const std::string& fingerprint
    ) const = 0;
    virtual void write_skipped(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        const std::string& started_at,
        const std::string& finished_at,
        std::size_t source_count
    ) = 0;
    virtual void write_success(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        const std::string& date,
        const std::string& timestamp,
        const std::string& fingerprint,
        std::size_t source_count
    ) = 0;
};

} // namespace btrfsbackup::backup
