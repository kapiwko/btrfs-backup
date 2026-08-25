// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run.hpp>

#include <memory>
#include <stdexcept>

#include "support/test_helpers.hpp"

namespace {

class NoopEffects final : public btrfsbackup::IBackupRunActionEffects {
public:
    void execute_action(
        const btrfsbackup::BackupRunAction&,
        const btrfsbackup::BackupSourceRunPlan&,
        const btrfsbackup::BackupRunPlan&,
        btrfsbackup::CancellationToken&
    ) override {
    }
};

class UnusedTransferPipeline final : public btrfsbackup::IAsyncTransferPipeline {
public:
    std::unique_ptr<btrfsbackup::IAsyncTransferHandle> start(
        const btrfsbackup::TransferPipelinePlan&,
        btrfsbackup::ITransferEventSink&
    ) override {
        throw std::logic_error("empty backup run must not start a transfer");
    }
};

class NoopCheckpointStore final : public btrfsbackup::IBackupRunCheckpointStore {
public:
    void write_checkpoint(const btrfsbackup::BackupRunCheckpoint&) override {
    }
};

void test_backup_run_owns_plan_and_executes_once() {
    NoopEffects effects;
    UnusedTransferPipeline transfers;
    NoopCheckpointStore checkpoints;
    btrfsbackup::BackupRun run(
        btrfsbackup::BackupRunPlan{
            .profile_id = btrfsbackup::ProfileId{"default"},
            .run_id = btrfsbackup::RunId{"run-1"},
        },
        effects,
        transfers,
        checkpoints
    );

    test_helpers::expect_eq("owned profile", run.plan().profile_id.value, "default");
    test_helpers::expect_true("not started", !run.started(), "new run is already marked as started");

    btrfsbackup::NullBackupRunEventSink events;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::BackupRunExecutionResult result = run.execute(events, cancellation);
    test_helpers::expect_true("completed", result.completed, "empty run did not complete");
    test_helpers::expect_true("started", run.started(), "executed run is not marked as started");

    try {
        run.execute(events, cancellation);
        test_helpers::fail("execute once", "second execution did not fail");
    } catch (const std::logic_error&) {
    }
}

} // namespace

int main() {
    test_backup_run_owns_plan_and_executes_once();
    return test_helpers::finish("backup run tests");
}
