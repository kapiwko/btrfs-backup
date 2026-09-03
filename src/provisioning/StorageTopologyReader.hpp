// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <provisioning/StorageTopology.hpp>

namespace btrfsbackup::provisioning {

class StorageTopologyReader {
  public:
    virtual ~StorageTopologyReader() = default;
    [[nodiscard]] virtual StorageTopology scan() = 0;
};

} // namespace btrfsbackup::provisioning
