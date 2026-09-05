// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseDirectoryPageCollector.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace btrfsbackup::daemon::control {

BrowseDirectoryPageCollector::BrowseDirectoryPageCollector(std::size_t maximum_entries)
    : maximum_entries_(maximum_entries) {
    if (maximum_entries_ == 0)
        throw std::invalid_argument("browse page size must be positive");
    entries_.reserve(maximum_entries_ + 1);
}

bool BrowseDirectoryPageCollector::name_less(
    const BrowseEntryInfo& left,
    const BrowseEntryInfo& right
) {
    return left.name < right.name;
}

void BrowseDirectoryPageCollector::add(BrowseEntryInfo entry) {
    entries_.push_back(std::move(entry));
    std::ranges::push_heap(entries_, name_less);
    if (entries_.size() <= maximum_entries_ + 1)
        return;
    std::ranges::pop_heap(entries_, name_less);
    entries_.pop_back();
}

BrowseDirectoryPage BrowseDirectoryPageCollector::finish() {
    std::ranges::sort_heap(entries_, name_less);
    BrowseDirectoryPage page;
    if (entries_.size() > maximum_entries_) {
        entries_.resize(maximum_entries_);
        page.continuation_token = entries_.back().name;
    }
    page.entries = std::move(entries_);
    return page;
}

std::size_t BrowseDirectoryPageCollector::retained_entries() const noexcept {
    return entries_.size();
}

} // namespace btrfsbackup::daemon::control
