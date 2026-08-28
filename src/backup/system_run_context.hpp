// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/ports/run_context.hpp>

namespace btrfsbackup::backup {

class SystemClock final : public IClock {
  public:
    [[nodiscard]] std::string snapshot_timestamp() const override;
    [[nodiscard]] std::string local_date() const override;
    [[nodiscard]] std::string local_timestamp() const override;
};

class TimestampRunIdGenerator final : public IRunIdGenerator {
  public:
    [[nodiscard]] RunId generate(const std::string& snapshot_timestamp) override;
};

} // namespace btrfsbackup::backup
