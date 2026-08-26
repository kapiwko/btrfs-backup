// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <core/identifiers.hpp>

namespace btrfsbackup {

class IClock {
  public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::string snapshot_timestamp() const = 0;
    [[nodiscard]] virtual std::string local_date() const = 0;
    [[nodiscard]] virtual std::string local_timestamp() const = 0;
};

class IRunIdGenerator {
  public:
    virtual ~IRunIdGenerator() = default;
    [[nodiscard]] virtual RunId generate(const std::string& snapshot_timestamp) = 0;
};

} // namespace btrfsbackup
