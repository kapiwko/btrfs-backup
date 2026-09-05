// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

#include <daemon/control/BrowseSessionService.hpp>

namespace btrfsbackup::daemon::control {

class BrowseDirectoryPageCollector final {
  public:
    explicit BrowseDirectoryPageCollector(std::size_t maximum_entries);

    void add(BrowseEntryInfo entry);
    [[nodiscard]] BrowseDirectoryPage finish();
    [[nodiscard]] std::size_t retained_entries() const noexcept;

  private:
    [[nodiscard]] static bool name_less(const BrowseEntryInfo& left, const BrowseEntryInfo& right);

    std::size_t maximum_entries_;
    std::vector<BrowseEntryInfo> entries_;
};

} // namespace btrfsbackup::daemon::control
