// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <type_traits>
#include <utility>

#include <backup/model/backup_planning_snapshot.hpp>

#include "support/test_helpers.hpp"

namespace {

using btrfsbackup::backup::BackupPlanningSnapshot;
using btrfsbackup::backup::SnapshotInventoryBySource;

static_assert(!std::is_aggregate_v<BackupPlanningSnapshot>);
static_assert(std::is_same_v<
              decltype(std::declval<const BackupPlanningSnapshot&>().local_inventory()),
              const SnapshotInventoryBySource&>);

void test_snapshot_owns_discovered_values() {
    SnapshotInventoryBySource local_inventory{{"root", {}}};
    BackupPlanningSnapshot snapshot{
        local_inventory,
        {},
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
    };

    local_inventory.clear();

    test_helpers::expect_true(
        "snapshot owns local inventory",
        snapshot.local_inventory().contains("root"),
        "snapshot changed with discovery working data"
    );
    test_helpers::expect_eq(
        "snapshot owns profile state path",
        snapshot.profile_state_dir().string(),
        "/var/lib/btrfs-backup/profiles/default"
    );
}

} // namespace

int main() {
    test_snapshot_owns_discovered_values();
    return test_helpers::finish("backup planning snapshot tests");
}
