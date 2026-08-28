// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <core/identifiers.hpp>
#include <core/runtime_time.hpp>

namespace btrfsbackup::backup {

class IClock {
  public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual RuntimeTimePoint now() const = 0;
    [[nodiscard]] virtual LocalDate local_date() const = 0;
};

class IRunIdGenerator {
  public:
    virtual ~IRunIdGenerator() = default;
    [[nodiscard]] virtual RunId generate(RuntimeTimePoint time) = 0;
};

} // namespace btrfsbackup::backup
