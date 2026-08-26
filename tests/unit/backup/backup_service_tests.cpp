// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>

#include <memory>
#include <string>
#include <vector>

#include "support/test_helpers.hpp"

namespace {

struct FakeProfiles final : btrfsbackup::IProfileRepository {
    btrfsbackup::Profile profile{btrfsbackup::ProfileId{"default"}};
    btrfsbackup::ApplicationPaths paths;

    btrfsbackup::Profile get(const btrfsbackup::ProfileId&) const override {
        return profile;
    }
    const btrfsbackup::ApplicationPaths& application_paths() const override {
        return paths;
    }
    std::string fingerprint(const btrfsbackup::Profile&) const override {
        return "fingerprint";
    }
};

struct FakeMounts final : btrfsbackup::IMountInspector {
    mutable int calls = 0;
    std::vector<btrfsbackup::MountEntry> inspect() const override {
        ++calls;
        return {};
    }
};

struct FakeTargetManager final : btrfsbackup::ITargetManager {
    int calls = 0;
    void ensure_mounted(const btrfsbackup::Profile&) override {
        ++calls;
    }
};

struct FakePlanner final : btrfsbackup::IBackupPlanner {
    mutable int calls = 0;
    mutable std::string received_timestamp;
    btrfsbackup::BackupRunPlan build(
        const btrfsbackup::Profile& profile,
        const std::vector<btrfsbackup::MountEntry>&,
        const btrfsbackup::ApplicationPaths&,
        const btrfsbackup::RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override {
        ++calls;
        received_timestamp = snapshot_timestamp;
        return {.profile_id = btrfsbackup::ProfileId{profile.id}, .run_id = run_id};
    }
};

struct NoopCheckpoints final : btrfsbackup::IBackupRunCheckpointStore {
    void write_checkpoint(const btrfsbackup::BackupRunCheckpoint&) override {
    }
};

struct FakeRunFactory final : btrfsbackup::IBackupRunFactory {
    int calls = 0;
    btrfsbackup::BackupRunExecutionResult result{
        .outcome = btrfsbackup::BackupRunExecutionOutcome::Completed,
        .actions_completed = 3,
    };

    btrfsbackup::BackupRunExecutionResult execute(
        btrfsbackup::BackupRunPlan,
        btrfsbackup::IBackupRunEventSink&,
        btrfsbackup::IBackupRunCheckpointStore&,
        btrfsbackup::CancellationToken&
    ) override {
        ++calls;
        return result;
    }
};

struct FakeLease final : btrfsbackup::IBackupRunLease {};

struct FakeLocks final : btrfsbackup::IBackupLockManager {
    bool busy = false;
    int calls = 0;

    btrfsbackup::BackupRunLeaseResult try_acquire(const btrfsbackup::Profile&) override {
        ++calls;
        if (busy) {
            return {
                .lease = nullptr,
                .error_code = btrfsbackup::ErrorCode::RunnerProfileBusy,
                .error_message = "busy",
            };
        }
        return {
            .lease = std::make_unique<FakeLease>(),
            .error_code = std::nullopt,
            .error_message = {},
        };
    }
};

struct FakeState final : btrfsbackup::IRunStateRepository {
    bool daily_match = false;
    bool cancellation_requested = false;
    int skipped_writes = 0;
    int success_writes = 0;
    int cancel_writes = 0;
    std::string success_date;
    std::string success_timestamp;

    bool last_success_matches(
        const btrfsbackup::Profile&,
        const std::string&,
        const std::string&
    ) const override {
        return daily_match;
    }

    void write_skipped(
        const btrfsbackup::Profile&,
        const btrfsbackup::RunId&,
        const std::string&,
        const std::string&,
        std::size_t
    ) override {
        ++skipped_writes;
    }

    void write_success(
        const btrfsbackup::Profile&,
        const btrfsbackup::RunId&,
        const std::string& date,
        const std::string& timestamp,
        const std::string&,
        std::size_t
    ) override {
        ++success_writes;
        success_date = date;
        success_timestamp = timestamp;
    }

    std::unique_ptr<btrfsbackup::IBackupRunCheckpointStore> checkpoints(
        const btrfsbackup::ProfileId&
    ) override {
        return std::make_unique<NoopCheckpoints>();
    }

    std::unique_ptr<btrfsbackup::IBackupRunEventSink> events(
        btrfsbackup::BackupRunStatusDescription
    ) override {
        return std::make_unique<btrfsbackup::NullBackupRunEventSink>();
    }

    void request_cancel(const btrfsbackup::ProfileId&) override {
        ++cancel_writes;
    }
    bool cancel_requested(const btrfsbackup::ProfileId&) const override {
        return cancellation_requested;
    }
    void clear_cancel_request(const btrfsbackup::ProfileId&) override {
        cancellation_requested = false;
    }
};

struct FakeClock final : btrfsbackup::IClock {
    std::string snapshot_timestamp() const override {
        return "2026-08-26T120000Z";
    }
    std::string local_date() const override {
        return "2026-08-26";
    }
    std::string local_timestamp() const override {
        return "2026-08-26T14:00:00+0200";
    }
};

struct FakeCancellationWatch final : btrfsbackup::ICancellationWatch {};

struct FakeCancellationMonitor final : btrfsbackup::ICancellationMonitor {
    std::unique_ptr<btrfsbackup::ICancellationWatch> watch(
        const btrfsbackup::ProfileId&,
        btrfsbackup::CancellationToken&
    ) override {
        return std::make_unique<FakeCancellationWatch>();
    }
};

struct FakeRunIds final : btrfsbackup::IRunIdGenerator {
    btrfsbackup::RunId generate(const std::string&) override {
        return btrfsbackup::RunId{"run-1"};
    }
};

struct Fixture {
    FakeProfiles profiles;
    FakeMounts mounts;
    FakeTargetManager target;
    FakePlanner planner;
    FakeRunFactory runs;
    FakeLocks locks;
    FakeState state;
    FakeCancellationMonitor cancellation_monitor;
    FakeClock clock;
    FakeRunIds run_ids;
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::BackupService service;

    Fixture()
        : service(profiles, mounts, target, planner, runs, locks, state, cancellation_monitor, clock, run_ids, cancellation) {
        profiles.profile.name = "Default";
        profiles.profile.target.luks_uuid = "target-uuid";
        profiles.profile.settings.daily_limit = true;
    }
};

void test_success_uses_ports_and_persists_success() {
    Fixture fixture;
    const btrfsbackup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("completed", result.outcome == btrfsbackup::BackupExecutionOutcome::Completed, "run did not complete");
    test_helpers::expect_eq("run id", std::string(result.plan.run_id.value()), "run-1");
    test_helpers::expect_true("target manager calls", fixture.target.calls == 1, "unexpected call count");
    test_helpers::expect_true("mount inspector calls", fixture.mounts.calls == 1, "unexpected call count");
    test_helpers::expect_true("planner calls", fixture.planner.calls == 1, "unexpected call count");
    test_helpers::expect_true("run factory calls", fixture.runs.calls == 1, "unexpected call count");
    test_helpers::expect_true("success writes", fixture.state.success_writes == 1, "unexpected write count");
    test_helpers::expect_eq("planner timestamp", fixture.planner.received_timestamp, "2026-08-26T120000Z");
    test_helpers::expect_eq("success date", fixture.state.success_date, "2026-08-26");
    test_helpers::expect_eq("success timestamp", fixture.state.success_timestamp, "2026-08-26T14:00:00+0200");
}

void test_cancelled_run_does_not_persist_success() {
    Fixture fixture;
    fixture.runs.result = {
        .outcome = btrfsbackup::BackupRunExecutionOutcome::Cancelled,
        .actions_completed = 2,
    };

    const btrfsbackup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("cancelled", result.outcome == btrfsbackup::BackupExecutionOutcome::Cancelled, "cancelled outcome missing");
    test_helpers::expect_true("cancelled actions", result.actions_completed == 2, "completed action count was not preserved");
    test_helpers::expect_true("cancelled success writes", fixture.state.success_writes == 0, "cancelled run persisted success");
}

void test_busy_stops_before_target_access() {
    Fixture fixture;
    fixture.locks.busy = true;
    const btrfsbackup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("busy", result.outcome == btrfsbackup::BackupExecutionOutcome::Busy, "busy outcome missing");
    test_helpers::expect_true("target not called", fixture.target.calls == 0, "target manager was called");
    test_helpers::expect_true("planner not called", fixture.planner.calls == 0, "planner was called");
}

void test_daily_match_skips_execution() {
    Fixture fixture;
    fixture.state.daily_match = true;
    const btrfsbackup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("skipped", result.outcome == btrfsbackup::BackupExecutionOutcome::Skipped, "daily run not skipped");
    test_helpers::expect_true("run not called", fixture.runs.calls == 0, "run factory was called");
    test_helpers::expect_true("skipped status", fixture.state.skipped_writes == 1, "skipped status missing");
}

void test_cancel_validates_profile_and_writes_request() {
    Fixture fixture;
    const btrfsbackup::CancelBackupResult result = fixture.service.cancel(btrfsbackup::ProfileId{"default"});

    test_helpers::expect_true("cancel requested", result.cancel_requested, "cancel request missing");
    test_helpers::expect_true("cancel writes", fixture.state.cancel_writes == 1, "cancel request missing");
}

} // namespace

int main() {
    test_success_uses_ports_and_persists_success();
    test_cancelled_run_does_not_persist_success();
    test_busy_stops_before_target_access();
    test_daily_match_skips_execution();
    test_cancel_validates_profile_and_writes_request();
    return test_helpers::finish("backup service tests");
}
