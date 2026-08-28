// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <backup/backup_run_checkpoint_policy.hpp>

#include "support/test_helpers.hpp"

namespace {

class RecordingCheckpointStore final : public btrfsbackup::backup::IBackupRunCheckpointStore {
  public:
    explicit RecordingCheckpointStore(std::vector<std::string>& calls) : calls_(calls) {
    }

    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) override {
        calls_.push_back("checkpoint");
        checkpoints.push_back(checkpoint);
    }

    std::vector<btrfsbackup::backup::BackupRunCheckpoint> checkpoints;

  private:
    std::vector<std::string>& calls_;
};

class RecordingEventSink final : public btrfsbackup::backup::IBackupRunEventSink {
  public:
    explicit RecordingEventSink(std::vector<std::string>& calls) : calls_(calls) {
    }

    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) override {
        calls_.push_back("event");
        events.push_back(event);
    }

    std::vector<btrfsbackup::backup::BackupRunEvent> events;

  private:
    std::vector<std::string>& calls_;
};

void test_writes_checkpoint_before_emitting_success_event() {
    std::vector<std::string> calls;
    RecordingCheckpointStore checkpoints(calls);
    RecordingEventSink events(calls);
    btrfsbackup::backup::BackupRunCheckpointPolicy policy(checkpoints);

    btrfsbackup::backup::BackupSourceRunPlan home{.source_id = btrfsbackup::SourceId{"home"}};
    btrfsbackup::backup::BackupSourceRunPlan root{.source_id = btrfsbackup::SourceId{"root"}};
    btrfsbackup::backup::BackupRunPlan plan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
        .sources = {home, root},
    };
    const btrfsbackup::backup::BackupRunAction action =
        btrfsbackup::backup::CleanupIncomingAction{root.source_id, "/mnt/backup/.incoming/root"};

    policy.after_success(action, plan, plan.sources.at(1), events);

    test_helpers::expect_eq("call count", std::to_string(calls.size()), "2");
    test_helpers::expect_eq("durable write first", calls.at(0), "checkpoint");
    test_helpers::expect_eq("event second", calls.at(1), "event");
    test_helpers::expect_eq("checkpoint count", std::to_string(checkpoints.checkpoints.size()), "1");
    test_helpers::expect_eq(
        "checkpoint source",
        std::string(checkpoints.checkpoints.at(0).source_id.value()),
        "root"
    );
    test_helpers::expect_true(
        "checkpoint action",
        checkpoints.checkpoints.at(0).action_kind == btrfsbackup::backup::BackupRunActionKind::CleanupIncoming,
        "wrong checkpoint action"
    );
    test_helpers::expect_eq("event count", std::to_string(events.events.size()), "1");
    const auto* checkpoint_written = std::get_if<btrfsbackup::backup::CheckpointWritten>(&events.events.at(0));
    test_helpers::expect_true(
        "checkpoint event",
        checkpoint_written != nullptr,
        "wrong event kind"
    );
    if (checkpoint_written != nullptr) {
        test_helpers::expect_eq("source index", std::to_string(checkpoint_written->source_index), "2");
        test_helpers::expect_true(
            "checkpoint event action",
            checkpoint_written->action_kind == btrfsbackup::backup::BackupRunActionKind::CleanupIncoming,
            "checkpoint event lost its action"
        );
    }
}

} // namespace

int main() {
    test_writes_checkpoint_before_emitting_success_event();
    return test_helpers::finish("backup run checkpoint policy tests");
}
