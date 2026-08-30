// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/execution/BackupRun.hpp>
#include <backup/testing/NullBackupRunEventSink.hpp>

#include <memory>
#include <stdexcept>

#include "support/FakeSafeDirectory.hpp"
#include "support/TestHelpers.hpp"

namespace {

class NoopActionExecutor final : public btrfsbackup::backup::execution::IBackupActionExecutor {
  public:
    btrfsbackup::backup::execution::BackupActionExecutionResult execute(
        const btrfsbackup::backup::BackupRunAction&,
        btrfsbackup::backup::execution::BackupActionExecutionContext&
    ) override {
        return {};
    }
};

class NoopCheckpointStore final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint&) override {
    }
};

void test_backup_run_owns_plan_and_executes_once() {
    NoopActionExecutor action_executor;
    NoopCheckpointStore checkpoints;
    btrfsbackup::backup::execution::BackupRun run(
        btrfsbackup::backup::BackupRunPlan{
            .profile_id = btrfsbackup::ProfileId{"default"},
            .run_id = btrfsbackup::RunId{"run-1"},
        },
        action_executor,
        checkpoints
    );

    test_helpers::expect_eq("owned profile", std::string(run.plan().profile_id.value()), "default");
    test_helpers::expect_true("not started", !run.started(), "new run is already marked as started");

    btrfsbackup::backup::NullBackupRunEventSink events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::BackupRunExecutionResult result = run.execute(events, cancellation);
    test_helpers::expect_true(
        "completed",
        std::holds_alternative<btrfsbackup::backup::BackupRunExecutionCompleted>(result),
        "empty run did not complete"
    );
    test_helpers::expect_true("started", run.started(), "executed run is not marked as started");

    try {
        (void)run.execute(events, cancellation);
        test_helpers::fail("execute once", "second execution did not fail");
    } catch (const std::logic_error&) {
    }
}

} // namespace

int main() {
    test_backup_run_owns_plan_and_executes_once();
    return test_helpers::finish("backup run tests");
}
