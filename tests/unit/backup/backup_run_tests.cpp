// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run.hpp>

#include <memory>
#include <stdexcept>

#include "support/fake_safe_directory.hpp"
#include "support/test_helpers.hpp"

namespace {

class NoopActionHandler final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    void handle(
        const btrfsbackup::backup::BackupRunAction&,
        const btrfsbackup::backup::BackupRunPlan&,
        btrfsbackup::CancellationToken&
    ) override {
    }
};

class UnusedTransferPipeline final : public btrfsbackup::backup::transfer::IAsyncTransferPipeline {
  public:
    std::unique_ptr<btrfsbackup::backup::transfer::IAsyncTransferHandle> start(
        const btrfsbackup::backup::transfer::TransferPipelinePlan&,
        btrfsbackup::backup::transfer::ITransferEventSink&
    ) override {
        throw std::logic_error("empty backup run must not start a transfer");
    }
};

class NoopCheckpointStore final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint&) override {
    }
};

void test_backup_run_owns_plan_and_executes_once() {
    NoopActionHandler handler;
    UnusedTransferPipeline transfers;
    NoopCheckpointStore checkpoints;
    test_support::FakeSafeDirectoryRootFactory safe_directories;
    btrfsbackup::backup::BackupRun run(
        btrfsbackup::backup::BackupRunPlan{
            .profile_id = btrfsbackup::ProfileId{"default"},
            .run_id = btrfsbackup::RunId{"run-1"},
        },
        handler,
        transfers,
        checkpoints,
        safe_directories
    );

    test_helpers::expect_eq("owned profile", std::string(run.plan().profile_id.value()), "default");
    test_helpers::expect_true("not started", !run.started(), "new run is already marked as started");

    btrfsbackup::backup::NullBackupRunEventSink events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::backup::BackupRunExecutionResult result = run.execute(events, cancellation);
    test_helpers::expect_true(
        "completed",
        result.outcome == btrfsbackup::backup::BackupRunExecutionOutcome::Completed,
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
