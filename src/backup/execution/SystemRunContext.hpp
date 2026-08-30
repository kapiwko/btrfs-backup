// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/RunContext.hpp>

namespace btrfsbackup::backup::execution {

class SystemClock final : public IClock {
  public:
    [[nodiscard]] RuntimeTimePoint now() const override;
    [[nodiscard]] LocalDate local_date() const override;
};

class TimestampRunIdGenerator final : public IRunIdGenerator {
  public:
    [[nodiscard]] RunId generate(RuntimeTimePoint time) override;
};

} // namespace btrfsbackup::backup::execution
