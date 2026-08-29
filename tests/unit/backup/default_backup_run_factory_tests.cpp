// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/default_backup_run_factory.hpp>
#include <backup/testing/null_backup_run_event_sink.hpp>

#include "support/fake_safe_directory.hpp"
#include "support/test_helpers.hpp"

namespace {

class NoopActionHandler final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    explicit NoopActionHandler(int& destroyed) : destroyed_(destroyed) {
    }

    ~NoopActionHandler() override {
        ++destroyed_;
    }

    void handle(
        const btrfsbackup::backup::BackupRunAction&,
        const btrfsbackup::backup::BackupRunPlan&,
        btrfsbackup::CancellationToken&
    ) override {
    }

  private:
    int& destroyed_;
};

class RecordingActionHandlerFactory final
    : public btrfsbackup::backup::IBackupRunActionHandlerFactory {
  public:
    std::unique_ptr<btrfsbackup::backup::IBackupRunActionHandler> create(
        const btrfsbackup::backup::BackupRunPlan& plan
    ) override {
        ++created;
        target_mount_points.push_back(plan.target_mount_point.string());
        return std::make_unique<NoopActionHandler>(destroyed);
    }

    int created = 0;
    int destroyed = 0;
    std::vector<std::string> target_mount_points;
};

class UnusedTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    btrfsbackup::backup::transfer::TransferResult run(
        const btrfsbackup::backup::transfer::TransferPipelinePlan&,
        btrfsbackup::backup::transfer::ITransferEventSink&,
        btrfsbackup::CancellationToken&
    ) override {
        throw std::logic_error("empty backup run must not start a transfer");
    }
};

class NoopCheckpointStore final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint&) override {
    }
};

void test_default_factory_creates_run_scoped_action_handlers() {
    RecordingActionHandlerFactory action_handlers;
    UnusedTransferPipeline transfers;
    test_support::FakeSafeDirectoryRootFactory safe_directories;
    btrfsbackup::backup::DefaultBackupRunFactory factory(
        action_handlers,
        transfers,
        safe_directories
    );
    btrfsbackup::backup::NullBackupRunEventSink events;
    NoopCheckpointStore checkpoints;
    btrfsbackup::CancellationToken cancellation;

    for (const std::string& run_id : {"run-1", "run-2"}) {
        btrfsbackup::backup::BackupRunExecutionResult result = factory.execute(
            {
                .profile_id = btrfsbackup::ProfileId{"default"},
                .run_id = btrfsbackup::RunId{run_id},
                .target_mount_point = "/mnt/backup/" + run_id,
            },
            events,
            checkpoints,
            cancellation
        );
        test_helpers::expect_true(
            "completed " + run_id,
            std::holds_alternative<btrfsbackup::backup::BackupRunExecutionCompleted>(result),
            "empty run did not complete"
        );
    }

    test_helpers::expect_true("handler count", action_handlers.created == 2, "handler was not created per run");
    test_helpers::expect_true("handler lifetime", action_handlers.destroyed == 2, "handler outlived its run");
    test_helpers::expect_eq(
        "first target",
        action_handlers.target_mount_points.at(0),
        "/mnt/backup/run-1"
    );
    test_helpers::expect_eq(
        "second target",
        action_handlers.target_mount_points.at(1),
        "/mnt/backup/run-2"
    );
}

} // namespace

int main() {
    test_default_factory_creates_run_scoped_action_handlers();
    return test_helpers::finish("default backup run factory tests");
}
