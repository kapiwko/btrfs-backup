// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/test_helpers.hpp"

namespace {

struct FakeProfiles final : btrfsbackup::config::IProfileRepository {
    btrfsbackup::config::Profile profile{
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{""},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{"/mnt/backup/snapshots"},
            btrfsbackup::config::IncomingRoot{"/mnt/backup/.incoming"},
        },
    };

    btrfsbackup::config::LoadedProfile get(const btrfsbackup::ProfileId&) const override {
        return {
            .profile = profile,
            .fingerprint = btrfsbackup::config::ConfigurationFingerprint("fingerprint"),
            .generation = btrfsbackup::config::ConfigurationGeneration(profile.configuration_generation),
        };
    }
};

struct FakePreflight final : btrfsbackup::backup::IBackupPreflight {
    int calls = 0;
    void run(const btrfsbackup::config::Profile&) override {
        ++calls;
    }
};

struct FakeDiscovery final : btrfsbackup::backup::IBackupDiscovery {
    mutable int calls = 0;

    btrfsbackup::backup::BackupPlanningSnapshot discover(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::config::ApplicationPaths&
    ) const override {
        ++calls;
        return {{}, {}, {}, {}, "/state/from-discovery"};
    }
};

struct FakePlanBuilder final : btrfsbackup::backup::IBackupPlanBuilder {
    mutable int calls = 0;
    mutable std::string received_timestamp;
    mutable std::filesystem::path received_profile_state_dir;
    btrfsbackup::backup::BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::backup::BackupPlanningSnapshot& snapshot,
        const btrfsbackup::RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override {
        ++calls;
        received_timestamp = snapshot_timestamp;
        received_profile_state_dir = snapshot.profile_state_dir();
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
    btrfsbackup::backup::BackupRunExecutionResult result =
        btrfsbackup::backup::BackupRunExecutionCompleted{3};

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
            return btrfsbackup::backup::BackupRunExecutionCancelled{0};
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
            return btrfsbackup::backup::BackupRunLeaseBusy{
                .error_code = btrfsbackup::ErrorCode::RunnerProfileBusy,
                .error_message = "busy",
            };
        }
        return btrfsbackup::backup::BackupRunLeaseAcquired{
            .lease = std::make_unique<FakeLease>(destructions),
        };
    }
};

struct FakeActiveRunRegistration final : btrfsbackup::backup::IActiveRunRegistration {
    FakeActiveRunRegistration(
        std::optional<btrfsbackup::RunId>& active_run,
        bool& cancellation_requested,
        std::vector<std::string>* lifecycle
    )
        : active_run_(active_run),
          cancellation_requested_(cancellation_requested),
          lifecycle_(lifecycle) {
    }

    ~FakeActiveRunRegistration() override {
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("active-run");
        }
        active_run_.reset();
        cancellation_requested_ = false;
    }

  private:
    std::optional<btrfsbackup::RunId>& active_run_;
    bool& cancellation_requested_;
    std::vector<std::string>* lifecycle_;
};

struct FakeState final : btrfsbackup::backup::IRunLedger,
                         btrfsbackup::backup::IRunEventSinkFactory,
                         btrfsbackup::backup::ICheckpointStoreFactory,
                         btrfsbackup::backup::ICancellationRequestStore {
    bool daily_match = false;
    bool cancellation_requested = false;
    std::optional<btrfsbackup::RunId> active_run;
    std::vector<std::string>* lifecycle = nullptr;
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
        btrfsbackup::LocalDate,
        const std::string& fingerprint
    ) const override {
        matched_fingerprint = fingerprint;
        return daily_match;
    }

    void write_skipped(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::RunId&,
        btrfsbackup::RuntimeTimePoint,
        btrfsbackup::RuntimeTimePoint,
        std::size_t
    ) override {
        ++skipped_writes;
    }

    void write_success(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::RunId&,
        btrfsbackup::LocalDate date,
        btrfsbackup::RuntimeTimePoint timestamp,
        const std::string& fingerprint,
        std::size_t
    ) override {
        ++success_writes;
        success_date = btrfsbackup::format_local_date(date);
        success_timestamp = btrfsbackup::format_local_timestamp(timestamp);
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

    std::unique_ptr<btrfsbackup::backup::IActiveRunRegistration> register_active_run(
        const btrfsbackup::backup::CancellationRequest& request
    ) override {
        active_run = request.run_id;
        cancellation_requested = false;
        return std::make_unique<FakeActiveRunRegistration>(
            active_run,
            cancellation_requested,
            lifecycle
        );
    }

    btrfsbackup::backup::CancellationRequestOutcome request_cancel(
        const btrfsbackup::backup::CancellationRequest& request
    ) override {
        if (!active_run.has_value()) {
            return btrfsbackup::backup::CancellationRequestOutcome::StaleRun;
        }
        if (*active_run != request.run_id) {
            return btrfsbackup::backup::CancellationRequestOutcome::RunMismatch;
        }
        ++cancel_writes;
        cancellation_requested = true;
        return btrfsbackup::backup::CancellationRequestOutcome::Accepted;
    }
    bool cancel_requested(const btrfsbackup::backup::CancellationRequest& request) const override {
        return cancellation_requested && active_run.has_value() && *active_run == request.run_id;
    }
    void clear_cancel_request(const btrfsbackup::backup::CancellationRequest& request) override {
        if (active_run.has_value() && *active_run == request.run_id) {
            cancellation_requested = false;
        }
    }
};

struct FakeClock final : btrfsbackup::backup::IClock {
    btrfsbackup::RuntimeTimePoint now() const override {
        return *btrfsbackup::parse_utc_timestamp("2026-08-26T12:00:00Z");
    }
    btrfsbackup::LocalDate local_date() const override {
        return *btrfsbackup::parse_local_date("2026-08-26");
    }
};

struct FakeCancellationWatch final : btrfsbackup::backup::ICancellationWatch {
    FakeCancellationWatch(int& destructions, std::vector<std::string>* lifecycle)
        : destructions_(destructions), lifecycle_(lifecycle) {
    }
    ~FakeCancellationWatch() override {
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("watcher");
        }
        ++destructions_;
    }

  private:
    int& destructions_;
    std::vector<std::string>* lifecycle_;
};

struct FakeCancellationMonitor final : btrfsbackup::backup::ICancellationMonitor {
    int watch_destructions = 0;
    std::vector<std::string>* lifecycle = nullptr;

    std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch(
        const btrfsbackup::backup::CancellationRequest&,
        btrfsbackup::CancellationToken&
    ) override {
        return std::make_unique<FakeCancellationWatch>(watch_destructions, lifecycle);
    }
};

struct FakeRunIds final : btrfsbackup::backup::IRunIdGenerator {
    btrfsbackup::RunId generate(btrfsbackup::RuntimeTimePoint) override {
        return btrfsbackup::RunId{"run-1"};
    }
};

struct Fixture {
    FakeProfiles profiles;
    FakePreflight preflight;
    FakeDiscovery discovery;
    FakePlanBuilder plan_builder;
    FakeRunFactory runs;
    FakeLeases leases;
    FakeState state;
    FakeCancellationMonitor cancellation_monitor;
    FakeClock clock;
    FakeRunIds run_ids;
    std::vector<std::string> cancellation_lifecycle;
    btrfsbackup::config::ApplicationPaths paths;
    btrfsbackup::backup::BackupService service;

    Fixture()
        : service(
              profiles,
              paths,
              preflight,
              discovery,
              plan_builder,
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
        state.lifecycle = &cancellation_lifecycle;
        cancellation_monitor.lifecycle = &cancellation_lifecycle;
        profiles.profile.name = "Default";
        profiles.profile.settings.daily_limit = true;
    }
};

void test_success_uses_ports_and_persists_success() {
    Fixture fixture;
    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* completed = std::get_if<btrfsbackup::backup::BackupExecutionCompleted>(&result);
    test_helpers::expect_true("completed", completed != nullptr, "run did not complete");
    if (completed != nullptr) {
        test_helpers::expect_eq("run id", std::string(completed->plan.run_id.value()), "run-1");
    }
    test_helpers::expect_true("preflight calls", fixture.preflight.calls == 1, "unexpected call count");
    test_helpers::expect_true("discovery calls", fixture.discovery.calls == 1, "unexpected call count");
    test_helpers::expect_true("plan builder calls", fixture.plan_builder.calls == 1, "unexpected call count");
    test_helpers::expect_true("run factory calls", fixture.runs.calls == 1, "unexpected call count");
    test_helpers::expect_true("success writes", fixture.state.success_writes == 1, "unexpected write count");
    test_helpers::expect_eq("planner timestamp", fixture.plan_builder.received_timestamp, "2026-08-26T120000Z");
    test_helpers::expect_eq(
        "discovery passed to builder",
        fixture.plan_builder.received_profile_state_dir.string(),
        "/state/from-discovery"
    );
    test_helpers::expect_eq("success date", fixture.state.success_date, "2026-08-26");
    test_helpers::expect_eq("success timestamp", fixture.state.success_timestamp, "2026-08-26T14:00:00+0200");
    test_helpers::expect_eq("success fingerprint", fixture.state.success_fingerprint, "fingerprint");
}

void test_cancelled_run_does_not_persist_success() {
    Fixture fixture;
    fixture.runs.result = btrfsbackup::backup::BackupRunExecutionCancelled{2};

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* cancelled = std::get_if<btrfsbackup::backup::BackupExecutionCancelled>(&result);
    test_helpers::expect_true("cancelled", cancelled != nullptr, "cancelled outcome missing");
    test_helpers::expect_true(
        "cancelled actions",
        cancelled != nullptr && cancelled->actions_completed == 2,
        "completed action count was not preserved"
    );
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

    test_helpers::expect_true("first run cancelled", std::holds_alternative<btrfsbackup::backup::BackupExecutionCancelled>(first), "first run did not cancel");
    test_helpers::expect_true("second run completed", std::holds_alternative<btrfsbackup::backup::BackupExecutionCompleted>(second), "second run inherited cancellation");
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
    test_helpers::expect_true(prefix + " active run", !fixture.state.active_run.has_value(), "active run was not released");
    test_helpers::expect_true(
        prefix + " cancellation lifecycle",
        fixture.cancellation_lifecycle == std::vector<std::string>{"active-run", "watcher"},
        "watcher stopped before cancellation admission was closed"
    );
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

    test_helpers::expect_true("busy", std::holds_alternative<btrfsbackup::backup::BackupExecutionBusy>(result), "busy outcome missing");
    test_helpers::expect_true("preflight not called", fixture.preflight.calls == 0, "preflight was called");
    test_helpers::expect_true("discovery not called", fixture.discovery.calls == 0, "discovery was called");
    test_helpers::expect_true("plan builder not called", fixture.plan_builder.calls == 0, "plan builder was called");
}

void test_daily_match_skips_execution() {
    Fixture fixture;
    fixture.state.daily_match = true;
    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true("skipped", std::holds_alternative<btrfsbackup::backup::BackupExecutionSkipped>(result), "daily run not skipped");
    test_helpers::expect_true("run not called", fixture.runs.calls == 0, "run factory was called");
    test_helpers::expect_true("skipped status", fixture.state.skipped_writes == 1, "skipped status missing");
}

void test_cancel_validates_profile_and_writes_request() {
    Fixture fixture;
    fixture.leases.busy = true;
    fixture.state.active_run = btrfsbackup::RunId{"run-1"};
    const btrfsbackup::backup::CancelBackupResult result = fixture.service.cancel({
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
    });

    test_helpers::expect_true("cancel requested", std::holds_alternative<btrfsbackup::backup::CancellationAccepted>(result), "cancel request missing");
    test_helpers::expect_true("cancel writes", fixture.state.cancel_writes == 1, "cancel request missing");
}

void test_cancel_rejects_stale_run_when_lease_is_available() {
    Fixture fixture;
    fixture.state.active_run = btrfsbackup::RunId{"stale-run"};

    const btrfsbackup::backup::CancelBackupResult result = fixture.service.cancel({
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"stale-run"},
    });

    test_helpers::expect_true("stale cancel rejected", std::holds_alternative<btrfsbackup::backup::CancellationStaleRun>(result), "stale run was cancelled");
    test_helpers::expect_true("stale cancel writes", fixture.state.cancel_writes == 0, "stale request was written");
}

void test_cancel_rejects_mismatched_run() {
    Fixture fixture;
    fixture.leases.busy = true;
    fixture.state.active_run = btrfsbackup::RunId{"active-run"};

    const btrfsbackup::backup::CancelBackupResult result = fixture.service.cancel({
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"other-run"},
    });

    test_helpers::expect_true("mismatched cancel rejected", std::holds_alternative<btrfsbackup::backup::CancellationRunMismatch>(result), "mismatched run was cancelled");
    test_helpers::expect_true("mismatched cancel writes", fixture.state.cancel_writes == 0, "mismatched request was written");
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
    test_cancel_rejects_stale_run_when_lease_is_available();
    test_cancel_rejects_mismatched_run();
    return test_helpers::finish("backup service tests");
}
