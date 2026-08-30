// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <backup/model/TargetStorageMeasurement.hpp>
#include <config/domain/Profile.hpp>

namespace btrfsbackup::backup {

class ITargetStorageMeasurementReader {
  public:
    virtual ~ITargetStorageMeasurementReader() = default;

    [[nodiscard]] virtual std::optional<TargetStorageMeasurement> read_matching(
        const btrfsbackup::config::Profile& profile
    ) const = 0;
};

class ITargetStorageMeasurementStore : public ITargetStorageMeasurementReader {
  public:
    ~ITargetStorageMeasurementStore() override = default;

    virtual void write(
        const btrfsbackup::config::Profile& profile,
        const TargetStorageMeasurement& measurement
    ) = 0;
};

} // namespace btrfsbackup::backup
