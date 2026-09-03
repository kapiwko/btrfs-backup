// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

#include <provisioning/DevicePreparationTransaction.hpp>

namespace btrfsbackup::daemon::control {

using provisioning::DevicePreparationTransaction;

class DevicePreparationTransactionCodec final {
  public:
    [[nodiscard]] std::string serialize(const DevicePreparationTransaction& transaction) const;
    [[nodiscard]] DevicePreparationTransaction deserialize(std::string_view document) const;
};

} // namespace btrfsbackup::daemon::control
