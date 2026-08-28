// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>

#include <memory>
#include <string>
#include <vector>

#include "support/test_helpers.hpp"

namespace {

struct FakeProfiles final : btrfsbackup::config::IProfileRepository {
    btrfsbackup::config::Profile profile{btrfsbackup::ProfileId{"default"}};

    btrfsbackup::config::LoadedProfile get(const btrfsbackup::ProfileId&) const override {
        return {
            .profile = profile,
            .fingerprint = btrfsbackup::config::ConfigurationFingerprint("fingerprint"),
            .generation = btrfsbackup::config::ConfigurationGeneration(profile.configuration_generation),
        };
    }
};

struct FakeMounts final : btrfsbackup::backup::IMountInspector {
    mutable int calls = 0;
    std::vector<btrfsbackup::backup::MountEntry> inspect() const override {
        ++calls;
        return {};
    }
};

struct FakeTargetMounter final : btrfsbackup::backup::ITargetManager {
    int calls = 0;
    void ensure_mounted(const btrfsbackup::config::Profile&) override {
        ++calls;
    }
};

struct FakePlanner final : btrfsbackup::backup::IBackupPlanner {
    mutable int calls = 0;
    mutable std::string received_timestamp;
    btrfsbackup::backup::BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const std::vector<btrfsbackup::backup::MountEntry>&,
        const btrfsbackup::config::ApplicationPaths&,
        const btrfsbackup::RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override {
        ++calls;
        received_timestamp = snapshot_timestamp;
        return {.profile_id = btrfsbackup::ProfileId{profile.id}, .run_id = run_id};
    }
};

struct NoopCheckpoints final : btrfsbackup::backup::IBackupRunCheckpointStore {
    explicit NoopCheckpoints(int* destructions = nullptr) : destructions_(destructions) {
    }
    ~NoopCheckpoints() override {
        if (destructions_ != nullptr) {
            ++*destructions_;
        }
    }
    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint&) override {
    }

  private:
    int* destructions_;
};

struct TrackingEvents final : btrfsbackup::backup::IBackupRunEventSink {
    explicit TrackingEvents(int& destructions) : destructions_(destructions) {
    }
    ~TrackingEvents() override {
        ++destructions_;
    }
    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent&) override {
    }

  private:
    int& destructions_;
};

struct FakeRunFactory final : btrfsbackup::backup::IBackupRunFactory {
    int calls = 0;
    bool cancel_next = false;
    bool throw_next = false;
    std::vector<bool> cancellation_at_start;
    btrfsbackup::backup::BackupRunExecutionResult result{
        .outcome = btrfsbackup::backup::BackupRunExecutionOutcome::Completed,
        .actions_completed = 3,
    };

    btrfsbackup::backup::BackupRunExecutionResult execute(
        btrfsbackup::backup::BackupRunPlan,
        btrfsbackup::backup::IBackupRunEventSink&,
        btrfsbackup::backup::IBackupRunCheckpointStore&,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        ++calls;
        cancellation_at_start.push_back(cancellation.cancellation_requested());
        if (cancel_next) {
            cancel_next = false;
            cancellation.request_cancel();
            return {
                .outcome = btrfsbackup::backup::BackupRunExecutionOutcome::Cancelled,
                .actions_completed = 0,
            };
        }
        if (throw_next) {
            throw_next = false;
            throw std::runtime_error("run failed");
        }
        return result;
    }
};

struct FakeLease final : btrfsbackup::backup::IBackupRunLease {
    explicit FakeLease(int& destructions) : destructions_(destructions) {
    }
    ~FakeLease() override {
        ++destructions_;
    }

  private:
    int& destructions_;
};

struct FakeLeases final : btrfsbackup::backup::IBackupRunLeaseProvider {
    bool busy = false;
    int calls = 0;
    int destructions = 0;

    btrfsbackup::backup::BackupRunLeaseResult try_acquire(const btrfsbackup::config::Profile&) override {
        ++calls;
        if (busy) {
            return {
                .lease = nullptr,
                .error_code = btrfsbackup::ErrorCode::RunnerProfileBusy,
                .error_message = "busy",
            };
        }
        return {
            .lease = std::make_unique<FakeLease>(destructions),
            .error_code = std::nullopt,
            .error_message = {},
        };
    }
};

struct FakeState final : btrfsbackup::backup::IRunLedger,
                         btrfsbackup::backup::IRunEventSinkFactory,
                         btrfsbackup::backup::ICheckpointStoreFactory,
                         btrfsbackup::backup::ICancellationRequestStore {
    bool daily_match = false;
    bool cancellation_requested = false;
    int skipped_writes = 0;
    int success_writes = 0;
    int cancel_writes = 0;
    int checkpoint_destructions = 0;
    int event_destructions = 0;
    std::string success_date;
    std::string success_timestamp;
    mutable std::string matched_fingerprint;
    std::string success_fingerprint;

    bool last_success_matches(
        const btrfsbackup::config::Profile&,
        const std::string&,
        const std::string& fingerprint
    ) const override {
        matched_fingerprint = fingerprint;
        return daily_match;
    }

    void write_skipped(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::RunId&,
        const std::string&,
        const std::string&,
        std::size_t
    ) override {
        ++skipped_writes;
    }

    void write_success(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::RunId&,
        const std::string& date,
        const std::string& timestamp,
        const std::string& fingerprint,
        std::size_t
    ) override {
        ++success_writes;
        success_date = date;
        success_timestamp = timestamp;
        success_fingerprint = fingerprint;
    }

    std::unique_ptr<btrfsbackup::backup::IBackupRunCheckpointStore> checkpoints(
        const btrfsbackup::ProfileId&
    ) override {
        return std::make_unique<NoopCheckpoints>(&checkpoint_destructions);
    }

    std::unique_ptr<btrfsbackup::backup::IBackupRunEventSink> events(
        btrfsbackup::backup::BackupRunStatusDescription
    ) override {
        return std::make_unique<TrackingEvents>(event_destructions);
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

struct FakeClock final : btrfsbackup::backup::IClock {
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

struct FakeCancellationWatch final : btrfsbackup::backup::ICancellationWatch {
    explicit FakeCancellationWatch(int& destructions) : destructions_(destructions) {
    }
    ~FakeCancellationWatch() override {
        ++destructions_;
    }

  private:
    int& destructions_;
};

struct FakeCancellationMonitor final : btrfsbackup::backup::ICancellationMonitor {
    int watch_destructions = 0;

    std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch(
        const btrfsbackup::ProfileId&,
        btrfsbackup::CancellationToken&
    ) override {
        return std::make_unique<FakeCancellationWatch>(watch_destructions);
    }
};

struct FakeRunIds final : btrfsbackup::backup::IRunIdGenerator {
    btrfsbackup::RunId generate(const std::string&) override {
        return btrfsbackup::RunId{"run-1"};
    }
};

struct Fixture {
    FakeProfiles profiles;
    FakeMounts mounts;
    FakeTargetMounter target;
    FakePlanner planner;
    FakeRunFactory runs;
    FakeLeases leases;
    FakeState state;
    FakeCancellationMonitor cancellation_monitor;
    FakeClock clock;
    FakeRunIds run_ids;
    btrfsbackup::config::ApplicationPaths paths;
    btrfsbackup::backup::BackupService service;

    Fixture()
        : service(
              profiles,
              paths,
              mounts,
              target,
              planner,
              runs,
              leases,
              state,
              state,
              state,
              state,
              cancellation_monitor,
              clock,
              run_ids
          ) {
        profiles.profile.name = "Default";
        profiles.profile.target.luks_uuid = "target-uuid";
        profiles.profile.settings.daily_limit = true;
    }
};

void test_success_uses_ports_and_persists_success() {
    Fixture fixture;
    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("completed", result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Completed, "run did not complete");
    test_helpers::expect_eq("run id", std::string(result.plan.run_id.value()), "run-1");
    test_helpers::expect_true("target mounter calls", fixture.target.calls == 1, "unexpected call count");
    test_helpers::expect_true("mount inspector calls", fixture.mounts.calls == 1, "unexpected call count");
    test_helpers::expect_true("planner calls", fixture.planner.calls == 1, "unexpected call count");
    test_helpers::expect_true("run factory calls", fixture.runs.calls == 1, "unexpected call count");
    test_helpers::expect_true("success writes", fixture.state.success_writes == 1, "unexpected write count");
    test_helpers::expect_eq("planner timestamp", fixture.planner.received_timestamp, "2026-08-26T120000Z");
    test_helpers::expect_eq("success date", fixture.state.success_date, "2026-08-26");
    test_helpers::expect_eq("success timestamp", fixture.state.success_timestamp, "2026-08-26T14:00:00+0200");
    test_helpers::expect_eq("success fingerprint", fixture.state.success_fingerprint, "fingerprint");
}

void test_cancelled_run_does_not_persist_success() {
    Fixture fixture;
    fixture.runs.result = {
        .outcome = btrfsbackup::backup::BackupRunExecutionOutcome::Cancelled,
        .actions_completed = 2,
    };

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("cancelled", result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Cancelled, "cancelled outcome missing");
    test_helpers::expect_true("cancelled actions", result.actions_completed == 2, "completed action count was not preserved");
    test_helpers::expect_true("cancelled success writes", fixture.state.success_writes == 0, "cancelled run persisted success");
}

void test_each_run_gets_a_fresh_cancellation_token() {
    Fixture fixture;
    fixture.runs.cancel_next = true;

    const btrfsbackup::backup::BackupExecutionResult first = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });
    const btrfsbackup::backup::BackupExecutionResult second = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("first run cancelled", first.outcome == btrfsbackup::backup::BackupExecutionOutcome::Cancelled, "first run did not cancel");
    test_helpers::expect_true("second run completed", second.outcome == btrfsbackup::backup::BackupExecutionOutcome::Completed, "second run inherited cancellation");
    test_helpers::expect_true(
        "fresh cancellation tokens",
        fixture.runs.cancellation_at_start == std::vector<bool>{false, false},
        "a run started with cancellation already requested"
    );
}

void expect_run_resources_released(const std::string& prefix, const Fixture& fixture) {
    test_helpers::expect_true(prefix + " lease", fixture.leases.destructions == 1, "lease was not released");
    test_helpers::expect_true(prefix + " watcher", fixture.cancellation_monitor.watch_destructions == 1, "watcher was not released");
    test_helpers::expect_true(prefix + " checkpoints", fixture.state.checkpoint_destructions == 1, "checkpoint store was not released");
    test_helpers::expect_true(prefix + " events", fixture.state.event_destructions == 1, "event sink was not released");
}

void test_run_context_releases_resources_after_success() {
    Fixture fixture;
    (void)fixture.service.start({.profile_id = btrfsbackup::ProfileId{"default"}});
    expect_run_resources_released("successful run", fixture);
}

void test_run_context_releases_resources_after_exception() {
    Fixture fixture;
    fixture.runs.throw_next = true;
    try {
        (void)fixture.service.start({.profile_id = btrfsbackup::ProfileId{"default"}});
        test_helpers::expect_true("run exception", false, "expected execution failure");
    } catch (const std::runtime_error&) {
    }
    expect_run_resources_released("failed run", fixture);
}

void test_busy_stops_before_target_access() {
    Fixture fixture;
    fixture.leases.busy = true;
    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("busy", result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Busy, "busy outcome missing");
    test_helpers::expect_true("target not called", fixture.target.calls == 0, "target mounter was called");
    test_helpers::expect_true("planner not called", fixture.planner.calls == 0, "planner was called");
}

void test_daily_match_skips_execution() {
    Fixture fixture;
    fixture.state.daily_match = true;
    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("skipped", result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Skipped, "daily run not skipped");
    test_helpers::expect_true("run not called", fixture.runs.calls == 0, "run factory was called");
    test_helpers::expect_true("skipped status", fixture.state.skipped_writes == 1, "skipped status missing");
}

void test_cancel_validates_profile_and_writes_request() {
    Fixture fixture;
    const btrfsbackup::backup::CancelBackupResult result = fixture.service.cancel(btrfsbackup::ProfileId{"default"});

    test_helpers::expect_true("cancel requested", result.cancel_requested, "cancel request missing");
    test_helpers::expect_true("cancel writes", fixture.state.cancel_writes == 1, "cancel request missing");
}

} // namespace

int main() {
    test_success_uses_ports_and_persists_success();
    test_cancelled_run_does_not_persist_success();
    test_each_run_gets_a_fresh_cancellation_token();
    test_run_context_releases_resources_after_success();
    test_run_context_releases_resources_after_exception();
    test_busy_stops_before_target_access();
    test_daily_match_skips_execution();
    test_cancel_validates_profile_and_writes_request();
    return test_helpers::finish("backup service tests");
}
