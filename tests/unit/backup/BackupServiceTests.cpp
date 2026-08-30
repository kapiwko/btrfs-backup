// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/BackupService.hpp>
#include <backup/RunExecutionContext.hpp>
#include <core/Errors.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/TestHelpers.hpp"

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
            .generation = profile.configuration_generation,
        };
    }
};

struct FakePreflight final : btrfsbackup::backup::IBackupPreflight {
    int calls = 0;
    bool fail = false;
    int session_destructions = 0;
    std::vector<std::string>* lifecycle = nullptr;
    std::vector<btrfsbackup::backup::TargetMountMode> modes;
    bool cancel_during_run = false;
    bool session_close_fail = false;
    bool active_run_registered_at_call = false;
    int cancellation_watches_at_call = 0;
    const std::optional<btrfsbackup::RunId>* active_run = nullptr;
    const int* cancellation_watch_calls = nullptr;
    btrfsbackup::CancellationToken* received_cancellation = nullptr;

    struct Session final : btrfsbackup::backup::IMountedTargetSession {
        Session(int& destructions, std::vector<std::string>* lifecycle, bool close_fail)
            : destructions(destructions), lifecycle(lifecycle), close_fail(close_fail) {
        }
        ~Session() override {
            (void)close();
            ++destructions;
        }
        std::optional<btrfsbackup::backup::TargetCleanupError> close() noexcept override {
            if (closed) {
                return close_error;
            }
            closed = true;
            if (lifecycle != nullptr) {
                lifecycle->push_back("target-session");
            }
            if (close_fail) {
                close_error = btrfsbackup::backup::TargetCleanupError{
                    btrfsbackup::backup::TargetCleanupStage::MountUnit,
                    "mnt-backup.mount",
                    1,
                    "could not stop target mount unit mnt-backup.mount (exit code 1)",
                };
            }
            return close_error;
        }
        bool mounted_by_this_session() const noexcept override {
            return false;
        }
        int& destructions;
        std::vector<std::string>* lifecycle;
        bool close_fail;
        bool closed = false;
        std::optional<btrfsbackup::backup::TargetCleanupError> close_error;
    };

    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> run(
        const btrfsbackup::config::Profile&,
        btrfsbackup::backup::TargetMountMode mode,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        ++calls;
        modes.push_back(mode);
        received_cancellation = &cancellation;
        active_run_registered_at_call = active_run != nullptr && active_run->has_value();
        cancellation_watches_at_call = cancellation_watch_calls == nullptr ? 0 : *cancellation_watch_calls;
        if (cancel_during_run) {
            cancellation.request_cancel();
        }
        if (fail) {
            throw btrfsbackup::CodedOperationError(
                btrfsbackup::ErrorCode::TargetBtrfsUuidMismatch,
                "target identity mismatch"
            );
        }
        return std::make_unique<Session>(session_destructions, lifecycle, session_close_fail);
    }
};

struct FakeDiscovery final : btrfsbackup::backup::IBackupDiscovery {
    mutable int calls = 0;
    mutable btrfsbackup::CancellationToken* received_cancellation = nullptr;

    btrfsbackup::backup::BackupPlanningSnapshot discover(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::config::ApplicationPaths&,
        btrfsbackup::CancellationToken& cancellation
    ) const override {
        ++calls;
        received_cancellation = &cancellation;
        return {{}, {}, {}, {}, "/state/from-discovery"};
    }
};

struct FakePlanBuilder final : btrfsbackup::backup::IBackupPlanBuilder {
    mutable int calls = 0;
    mutable std::string received_timestamp;
    mutable std::filesystem::path received_profile_state_dir;
    mutable btrfsbackup::CancellationToken* received_cancellation = nullptr;
    btrfsbackup::backup::BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::backup::BackupPlanningSnapshot& snapshot,
        const btrfsbackup::RunId& run_id,
        const std::string& snapshot_timestamp,
        btrfsbackup::CancellationToken& cancellation
    ) const override {
        ++calls;
        received_timestamp = snapshot_timestamp;
        received_profile_state_dir = snapshot.profile_state_dir();
        received_cancellation = &cancellation;
        return {.profile_id = btrfsbackup::ProfileId{profile.id}, .run_id = run_id};
    }
};

struct NoopCheckpoints final : btrfsbackup::backup::IBackupRunCheckpointStore {
    NoopCheckpoints(int* destructions = nullptr, std::vector<std::string>* lifecycle = nullptr)
        : destructions_(destructions), lifecycle_(lifecycle) {
    }
    ~NoopCheckpoints() override {
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("checkpoints");
        }
        if (destructions_ != nullptr) {
            ++*destructions_;
        }
    }
    void write_checkpoint(const btrfsbackup::backup::BackupRunCheckpoint&) override {
    }

  private:
    int* destructions_;
    std::vector<std::string>* lifecycle_;
};

struct TrackingEvents final : btrfsbackup::backup::IBackupRunEventSink {
    TrackingEvents(
        int& destructions,
        std::vector<btrfsbackup::backup::BackupRunEvent>& events,
        std::vector<std::string>* lifecycle,
        const bool& fail_run_completed
    )
        : destructions_(destructions),
          events_(events),
          lifecycle_(lifecycle),
          fail_run_completed_(fail_run_completed) {
    }
    ~TrackingEvents() override {
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("events");
        }
        ++destructions_;
    }
    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) override {
        if (fail_run_completed_ &&
            btrfsbackup::backup::backup_run_event_kind(event) ==
                btrfsbackup::backup::BackupRunEventKind::RunCompleted) {
            throw std::runtime_error("could not persist terminal status");
        }
        events_.push_back(event);
    }

  private:
    int& destructions_;
    std::vector<btrfsbackup::backup::BackupRunEvent>& events_;
    std::vector<std::string>* lifecycle_;
    const bool& fail_run_completed_;
};

struct FakeRunFactory final : btrfsbackup::backup::IBackupRunFactory {
    int calls = 0;
    bool cancel_next = false;
    bool throw_next = false;
    std::vector<bool> cancellation_at_start;
    btrfsbackup::backup::BackupRunExecutionResult result =
        btrfsbackup::backup::BackupRunExecutionCompleted{3};

    btrfsbackup::backup::BackupRunExecutionResult execute(
        btrfsbackup::backup::BackupRunPlan plan,
        btrfsbackup::backup::IBackupRunEventSink& events,
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
        if (const auto* failed = std::get_if<btrfsbackup::backup::BackupRunExecutionFailed>(&result)) {
            events.on_backup_run_event(btrfsbackup::backup::RunFailed{
                .profile_id = plan.profile_id,
                .run_id = plan.run_id,
                .error_code = failed->error_code,
                .message = failed->error_message,
            });
        }
        return result;
    }
};

struct FakeLease final : btrfsbackup::backup::IBackupRunLease {
    FakeLease(int& destructions, std::vector<std::string>* lifecycle)
        : destructions_(destructions), lifecycle_(lifecycle) {
    }
    ~FakeLease() override {
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("lease");
        }
        ++destructions_;
    }

  private:
    int& destructions_;
    std::vector<std::string>* lifecycle_;
};

struct FakeLeases final : btrfsbackup::backup::IBackupRunLeaseProvider {
    bool busy = false;
    int calls = 0;
    int destructions = 0;
    std::vector<std::string>* lifecycle = nullptr;

    btrfsbackup::backup::BackupRunLeaseResult try_acquire(const btrfsbackup::config::Profile&) override {
        ++calls;
        if (busy) {
            return btrfsbackup::backup::BackupRunLeaseBusy{
                .error_code = btrfsbackup::ErrorCode::RunnerProfileBusy,
                .error_message = "busy",
            };
        }
        return btrfsbackup::backup::BackupRunLeaseAcquired{
            .lease = std::make_unique<FakeLease>(destructions, lifecycle),
        };
    }
};

struct FakeActiveRunRegistration final : btrfsbackup::backup::IActiveRunRegistration {
    FakeActiveRunRegistration(
        std::optional<btrfsbackup::RunId>& active_run,
        std::vector<std::string>* lifecycle,
        std::optional<std::string> diagnostic
    )
        : active_run_(active_run),
          lifecycle_(lifecycle),
          diagnostic_(std::move(diagnostic)) {
    }

    ~FakeActiveRunRegistration() override = default;

    std::optional<std::string> close() override {
        if (closed_) {
            return std::nullopt;
        }
        closed_ = true;
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("active-run");
        }
        active_run_.reset();
        return diagnostic_;
    }

  private:
    std::optional<btrfsbackup::RunId>& active_run_;
    std::vector<std::string>* lifecycle_;
    std::optional<std::string> diagnostic_;
    bool closed_ = false;
};

struct FakeState final : btrfsbackup::backup::IRunLedger,
                         btrfsbackup::backup::IRunEventSinkFactory,
                         btrfsbackup::backup::ICheckpointStoreFactory,
                         btrfsbackup::backup::ICancellationRequestStore {
    bool daily_match = false;
    bool cancellation_requested = false;
    std::optional<btrfsbackup::RunId> active_run;
    std::vector<std::string>* lifecycle = nullptr;
    std::optional<std::string> active_close_diagnostic;
    bool fail_clear_cancel_request = false;
    int skipped_writes = 0;
    int success_writes = 0;
    bool fail_success_write = false;
    bool fail_run_completed = false;
    int cancel_writes = 0;
    int checkpoint_destructions = 0;
    int event_destructions = 0;
    std::vector<btrfsbackup::backup::BackupRunEvent> events_received;
    std::vector<btrfsbackup::backup::BackupRunStatusDescription> event_descriptions;
    std::string success_date;
    btrfsbackup::RuntimeTimePoint success_timestamp;
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
        if (fail_success_write) {
            throw btrfsbackup::CodedOperationError(
                btrfsbackup::ErrorCode::BackupFailed,
                "could not persist success"
            );
        }
        ++success_writes;
        success_date = btrfsbackup::format_local_date(date);
        success_timestamp = timestamp;
        success_fingerprint = fingerprint;
    }

    std::unique_ptr<btrfsbackup::backup::IBackupRunCheckpointStore> checkpoints(
        const btrfsbackup::ProfileId&
    ) override {
        return std::make_unique<NoopCheckpoints>(&checkpoint_destructions, lifecycle);
    }

    std::unique_ptr<btrfsbackup::backup::IBackupRunEventSink> events(
        btrfsbackup::backup::BackupRunStatusDescription description
    ) override {
        event_descriptions.push_back(std::move(description));
        return std::make_unique<TrackingEvents>(
            event_destructions,
            events_received,
            lifecycle,
            fail_run_completed
        );
    }

    std::unique_ptr<btrfsbackup::backup::IActiveRunRegistration> register_active_run(
        const btrfsbackup::backup::CancellationRequest& request
    ) override {
        active_run = request.run_id;
        cancellation_requested = false;
        return std::make_unique<FakeActiveRunRegistration>(
            active_run,
            lifecycle,
            active_close_diagnostic
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
        (void)request;
        if (lifecycle != nullptr) {
            lifecycle->push_back("cancellation-request");
        }
        cancellation_requested = false;
        if (fail_clear_cancel_request) {
            throw std::runtime_error("cancel cleanup failed");
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
    FakeCancellationWatch(
        int& destructions,
        std::vector<std::string>* lifecycle,
        std::optional<std::string> diagnostic
    )
        : destructions_(destructions), lifecycle_(lifecycle), diagnostic_(std::move(diagnostic)) {
    }
    ~FakeCancellationWatch() override = default;

    std::optional<std::string> close() override {
        if (closed_) {
            return std::nullopt;
        }
        closed_ = true;
        if (lifecycle_ != nullptr) {
            lifecycle_->push_back("watcher");
        }
        ++destructions_;
        return diagnostic_;
    }

  private:
    int& destructions_;
    std::vector<std::string>* lifecycle_;
    std::optional<std::string> diagnostic_;
    bool closed_ = false;
};

struct FakeCancellationMonitor final : btrfsbackup::backup::ICancellationMonitor {
    int watch_calls = 0;
    int watch_destructions = 0;
    std::vector<std::string>* lifecycle = nullptr;
    std::optional<std::string> close_diagnostic;

    std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch(
        const btrfsbackup::backup::CancellationRequest&,
        btrfsbackup::CancellationToken&
    ) override {
        ++watch_calls;
        return std::make_unique<FakeCancellationWatch>(watch_destructions, lifecycle, close_diagnostic);
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
    btrfsbackup::backup::RunSessionFactory sessions;
    std::vector<std::string> cancellation_lifecycle;
    btrfsbackup::config::ApplicationPaths paths;
    btrfsbackup::backup::BackupService service;

    Fixture()
        : sessions(leases, state, state, state, cancellation_monitor),
          service(
              profiles,
              paths,
              preflight,
              discovery,
              plan_builder,
              runs,
              state,
              sessions,
              clock,
              run_ids
          ) {
        state.lifecycle = &cancellation_lifecycle;
        cancellation_monitor.lifecycle = &cancellation_lifecycle;
        preflight.lifecycle = &cancellation_lifecycle;
        preflight.active_run = &state.active_run;
        preflight.cancellation_watch_calls = &cancellation_monitor.watch_calls;
        leases.lifecycle = &cancellation_lifecycle;
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
    test_helpers::expect_true(
        "preparation uses run cancellation",
        fixture.preflight.received_cancellation != nullptr &&
            fixture.preflight.received_cancellation == fixture.discovery.received_cancellation &&
            fixture.discovery.received_cancellation == fixture.plan_builder.received_cancellation,
        "preflight, discovery, and planning did not share the run token"
    );
    test_helpers::expect_true("run factory calls", fixture.runs.calls == 1, "unexpected call count");
    test_helpers::expect_true("success writes", fixture.state.success_writes == 1, "unexpected write count");
    test_helpers::expect_eq("planner timestamp", fixture.plan_builder.received_timestamp, "2026-08-26T120000Z");
    test_helpers::expect_eq(
        "discovery passed to builder",
        fixture.plan_builder.received_profile_state_dir.string(),
        "/state/from-discovery"
    );
    test_helpers::expect_eq("success date", fixture.state.success_date, "2026-08-26");
    test_helpers::expect_true(
        "success timestamp",
        fixture.state.success_timestamp == fixture.clock.now(),
        "ledger received a different completion timestamp"
    );
    test_helpers::expect_eq("success fingerprint", fixture.state.success_fingerprint, "fingerprint");
    test_helpers::expect_true(
        "session event description",
        fixture.state.event_descriptions.size() == 1 &&
            fixture.state.event_descriptions.front().profile_name == "Default" &&
            fixture.state.event_descriptions.front().target_name == "backup" &&
            fixture.state.event_descriptions.front().started_at == fixture.clock.now(),
        "session factory did not derive event metadata from the loaded profile and run identity"
    );
}

void test_validate_only_emits_validation_lifecycle_without_backup_success() {
    Fixture fixture;
    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
        .validate_only = true,
    });

    test_helpers::expect_true(
        "validation result",
        std::holds_alternative<btrfsbackup::backup::BackupExecutionValidated>(result),
        "validation did not return its dedicated result"
    );
    test_helpers::expect_true("validation executor calls", fixture.runs.calls == 0, "validation executed backup actions");
    test_helpers::expect_true("validation success writes", fixture.state.success_writes == 0, "validation persisted backup success");
    test_helpers::expect_true(
        "validation event count",
        fixture.state.events_received.size() == 2,
        "validation emitted an unexpected event sequence"
    );
    if (fixture.state.events_received.size() == 2) {
        const auto* started = std::get_if<btrfsbackup::backup::RunStarted>(&fixture.state.events_received.at(0));
        test_helpers::expect_true(
            "validation start event",
            started != nullptr && started->operation_kind == btrfsbackup::backup::OperationKind::TargetValidation,
            "validation was reported as a backup run"
        );
        test_helpers::expect_true(
            "validation completion event",
            std::holds_alternative<btrfsbackup::backup::TargetValidationCompleted>(fixture.state.events_received.at(1)),
            "validation emitted backup completion"
        );
    }
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

void expect_run_resources_released(
    const std::string& prefix,
    const Fixture& fixture,
    bool target_closed_before_context = false
) {
    test_helpers::expect_true(prefix + " lease", fixture.leases.destructions == 1, "lease was not released");
    test_helpers::expect_true(prefix + " watcher", fixture.cancellation_monitor.watch_destructions == 1, "watcher was not released");
    test_helpers::expect_true(prefix + " checkpoints", fixture.state.checkpoint_destructions == 1, "checkpoint store was not released");
    test_helpers::expect_true(prefix + " events", fixture.state.event_destructions == 1, "event sink was not released");
    test_helpers::expect_true(prefix + " active run", !fixture.state.active_run.has_value(), "active run was not released");
    const std::vector<std::string> expected_lifecycle = target_closed_before_context
        ? std::vector<std::string>{
              "target-session",
              "watcher",
              "events",
              "checkpoints",
              "active-run",
              "cancellation-request",
              "lease",
          }
        : std::vector<std::string>{
              "watcher",
              "events",
              "checkpoints",
              "active-run",
              "cancellation-request",
              "target-session",
              "lease",
          };
    test_helpers::expect_true(
        prefix + " cancellation lifecycle",
        fixture.cancellation_lifecycle == expected_lifecycle,
        "run resources were not released in the safe order"
    );
}

void test_run_context_releases_resources_after_success() {
    Fixture fixture;
    (void)fixture.service.start({.profile_id = btrfsbackup::ProfileId{"default"}});
    expect_run_resources_released("successful run", fixture, true);
}

void test_run_context_close_aggregates_cleanup_diagnostics() {
    Fixture fixture;
    fixture.cancellation_monitor.close_diagnostic = "watch cleanup failed";
    fixture.state.active_close_diagnostic = "active run cleanup failed";
    fixture.state.fail_clear_cancel_request = true;
    fixture.preflight.session_close_fail = true;

    std::unique_ptr<btrfsbackup::backup::IBackupRunEventSink> events = fixture.state.events({});
    btrfsbackup::backup::BackupRunLeaseResult lease_result = fixture.leases.try_acquire(
        fixture.profiles.profile
    );
    std::unique_ptr<btrfsbackup::backup::IBackupRunLease> lease = std::move(
        std::get<btrfsbackup::backup::BackupRunLeaseAcquired>(lease_result).lease
    );
    btrfsbackup::CancellationToken cancellation;
    std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> target_session = fixture.preflight.run(
        fixture.profiles.profile,
        btrfsbackup::backup::TargetMountMode::MountIfNeeded,
        cancellation
    );
    btrfsbackup::backup::RunExecutionContext context(
        fixture.profiles.profile.id,
        btrfsbackup::RunId{"run-1"},
        events,
        std::move(lease),
        fixture.state,
        fixture.state,
        fixture.cancellation_monitor
    );
    context.attach_target_session(std::move(target_session));

    const btrfsbackup::backup::CloseResult result = context.close();
    test_helpers::expect_true("cleanup result failed", !result.succeeded(), "cleanup failures were lost");
    test_helpers::expect_true(
        "cleanup diagnostics",
        result.failures.size() == 4 &&
            result.failures.at(0).stage == btrfsbackup::backup::RunExecutionContextCloseStage::CancellationWatch &&
            result.failures.at(1).stage == btrfsbackup::backup::RunExecutionContextCloseStage::ActiveRun &&
            result.failures.at(2).stage == btrfsbackup::backup::RunExecutionContextCloseStage::CancellationRequest &&
            result.failures.at(3).stage == btrfsbackup::backup::RunExecutionContextCloseStage::TargetSession,
        "cleanup failures were not aggregated in lifecycle order"
    );
    test_helpers::expect_true("event sink closed", events == nullptr, "event sink remained open");
    test_helpers::expect_true(
        "idempotent close",
        context.close().succeeded(),
        "second close repeated cleanup failures"
    );
    expect_run_resources_released("diagnostic cleanup", fixture);
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
    test_helpers::expect_true(
        "unexpected failure lifecycle",
        fixture.state.events_received.size() == 2 &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(0)) ==
                btrfsbackup::backup::BackupRunEventKind::RunStarted &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(1)) ==
                btrfsbackup::backup::BackupRunEventKind::RunFailed,
        "unexpected failure was not recorded before rethrow"
    );
}

void test_success_ledger_failure_returns_degraded_success() {
    Fixture fixture;
    fixture.state.fail_success_write = true;

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* completed = std::get_if<btrfsbackup::backup::BackupExecutionCompleted>(&result);
    test_helpers::expect_true("ledger failure result", completed != nullptr, "ledger failure changed completed backup to failure");
    test_helpers::expect_true(
        "ledger failure warning",
        completed != nullptr && completed->warnings.size() == 1 &&
            completed->warnings.front().component ==
                btrfsbackup::backup::BackupCompletionWarningComponent::SuccessLedger,
        "ledger failure was not reported as degraded completion"
    );
    test_helpers::expect_true(
        "ledger failure terminal status",
        fixture.state.events_received.size() == 2 &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(0)) ==
                btrfsbackup::backup::BackupRunEventKind::RunStarted &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(1)) ==
                btrfsbackup::backup::BackupRunEventKind::RunCompleted,
        "ledger failure suppressed terminal status"
    );
}

void test_terminal_status_failure_returns_degraded_success() {
    Fixture fixture;
    fixture.state.fail_run_completed = true;

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* completed = std::get_if<btrfsbackup::backup::BackupExecutionCompleted>(&result);
    test_helpers::expect_true("terminal status failure result", completed != nullptr, "status failure changed completed backup to failure");
    test_helpers::expect_true("terminal status ledger", fixture.state.success_writes == 1, "status failure suppressed success ledger");
    test_helpers::expect_true(
        "terminal status warning",
        completed != nullptr && completed->warnings.size() == 1 &&
            completed->warnings.front().component ==
                btrfsbackup::backup::BackupCompletionWarningComponent::TerminalStatus,
        "terminal status failure was not reported as degraded completion"
    );
    test_helpers::expect_true(
        "terminal status no failure event",
        fixture.state.events_received.size() == 1 &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.front()) ==
                btrfsbackup::backup::BackupRunEventKind::RunStarted,
        "status persistence failure emitted RunFailed"
    );
}

void test_target_cleanup_failure_prevents_success() {
    Fixture fixture;
    fixture.preflight.session_close_fail = true;

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* failed = std::get_if<btrfsbackup::backup::BackupExecutionFailed>(&result);
    test_helpers::expect_true("target cleanup failure result", failed != nullptr, "cleanup failure did not fail the run");
    test_helpers::expect_true("target cleanup no success", fixture.state.success_writes == 0, "cleanup failure persisted success");
    test_helpers::expect_true(
        "target cleanup failure lifecycle",
        fixture.state.events_received.size() == 2 &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(0)) ==
                btrfsbackup::backup::BackupRunEventKind::RunStarted &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(1)) ==
                btrfsbackup::backup::BackupRunEventKind::RunFailed,
        "cleanup failure emitted RunCompleted"
    );
}

void test_preflight_failure_has_terminal_run_lifecycle() {
    Fixture fixture;
    fixture.preflight.fail = true;

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* failed = std::get_if<btrfsbackup::backup::BackupExecutionFailed>(&result);
    test_helpers::expect_true("preflight failure result", failed != nullptr, "preflight failure was not typed");
    if (failed != nullptr) {
        test_helpers::expect_eq(
            "preflight failure code",
            btrfsbackup::error_code_name(failed->error_code),
            "target.btrfs_uuid_mismatch"
        );
        test_helpers::expect_eq("preflight failure run", std::string(failed->run_id.value()), "run-1");
    }
    test_helpers::expect_true(
        "preflight failure lifecycle",
        fixture.state.events_received.size() == 2 &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(0)) ==
                btrfsbackup::backup::BackupRunEventKind::RunStarted &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(1)) ==
                btrfsbackup::backup::BackupRunEventKind::RunFailed,
        "preflight failure did not emit RunStarted followed by RunFailed"
    );
    test_helpers::expect_true("preflight failure lease release", fixture.leases.destructions == 1, "lease was not released");
    test_helpers::expect_true("preflight failure no executor", fixture.runs.calls == 0, "executor was called");
}

void test_run_can_be_cancelled_during_preflight() {
    Fixture fixture;
    fixture.preflight.cancel_during_run = true;

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    test_helpers::expect_true(
        "preflight cancellation result",
        std::holds_alternative<btrfsbackup::backup::BackupExecutionCancelled>(result),
        "preflight cancellation was not returned as a cancelled run"
    );
    test_helpers::expect_true(
        "active run before preflight",
        fixture.preflight.active_run_registered_at_call,
        "preflight started before the active run was registered"
    );
    test_helpers::expect_true(
        "cancellation watch before preflight",
        fixture.preflight.cancellation_watches_at_call == 1,
        "preflight started before the cancellation watch"
    );
    test_helpers::expect_true("cancelled before discovery", fixture.discovery.calls == 0, "discovery ran after preflight cancellation");
    test_helpers::expect_true(
        "preflight cancellation lifecycle",
        fixture.state.events_received.size() == 2 &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(0)) ==
                btrfsbackup::backup::BackupRunEventKind::RunStarted &&
            btrfsbackup::backup::backup_run_event_kind(fixture.state.events_received.at(1)) ==
                btrfsbackup::backup::BackupRunEventKind::RunCancelled,
        "preflight cancellation did not emit RunStarted followed by RunCancelled"
    );
    expect_run_resources_released("preflight cancellation", fixture);
}

void test_executor_typed_failure_is_not_emitted_twice() {
    Fixture fixture;
    fixture.runs.result = btrfsbackup::backup::BackupRunExecutionFailed{
        .error_code = btrfsbackup::ErrorCode::TransferProducerFailed,
        .error_message = "send failed",
        .actions_completed = 1,
    };

    const btrfsbackup::backup::BackupExecutionResult result = fixture.service.start({
        .profile_id = btrfsbackup::ProfileId{"default"},
    });

    const auto* failed = std::get_if<btrfsbackup::backup::BackupExecutionFailed>(&result);
    test_helpers::expect_true("executor typed failure result", failed != nullptr, "typed executor failure was lost");
    test_helpers::expect_true(
        "executor typed failure actions",
        failed != nullptr && failed->actions_completed == 1,
        "completed action count was not preserved"
    );
    const auto failure_events = std::count_if(
        fixture.state.events_received.begin(),
        fixture.state.events_received.end(),
        [](const btrfsbackup::backup::BackupRunEvent& event) {
            return btrfsbackup::backup::backup_run_event_kind(event) ==
                btrfsbackup::backup::BackupRunEventKind::RunFailed;
        }
    );
    test_helpers::expect_true("executor typed failure duplication", failure_events == 1, "service duplicated executor RunFailed");
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

void test_plan_acquires_lease_and_defaults_to_offline_target() {
    Fixture fixture;
    (void)fixture.service.plan({.profile_id = btrfsbackup::ProfileId{"default"}});

    test_helpers::expect_true("plan lease", fixture.leases.calls == 1, "plan did not acquire a lease");
    test_helpers::expect_true(
        "plan target mode",
        fixture.preflight.modes == std::vector{btrfsbackup::backup::TargetMountMode::RequireMounted},
        "plan did not use offline target preparation"
    );
    test_helpers::expect_true("plan session released", fixture.preflight.session_destructions == 1, "plan target session was not released");
    test_helpers::expect_true("plan lease released", fixture.leases.destructions == 1, "plan lease was not released");
}

void test_plan_can_explicitly_mount_target() {
    Fixture fixture;
    (void)fixture.service.plan({
        .profile_id = btrfsbackup::ProfileId{"default"},
        .mount_target = true,
    });

    test_helpers::expect_true(
        "mounted plan target mode",
        fixture.preflight.modes == std::vector{btrfsbackup::backup::TargetMountMode::MountIfNeeded},
        "mounted plan did not enable target activation"
    );
}

void test_plan_reports_target_cleanup_failure() {
    Fixture fixture;
    fixture.preflight.session_close_fail = true;

    try {
        (void)fixture.service.plan({
            .profile_id = btrfsbackup::ProfileId{"default"},
            .mount_target = true,
        });
        test_helpers::expect_true("plan cleanup failure", false, "plan succeeded despite cleanup failure");
    } catch (const btrfsbackup::CodedOperationError& error) {
        test_helpers::expect_contains("plan cleanup diagnostic", error.what(), "could not stop target mount unit");
    }
}

void test_busy_plan_stops_before_target_access() {
    Fixture fixture;
    fixture.leases.busy = true;
    try {
        (void)fixture.service.plan({.profile_id = btrfsbackup::ProfileId{"default"}});
        test_helpers::expect_true("busy plan exception", false, "busy plan did not fail");
    } catch (const btrfsbackup::CodedOperationError& error) {
        test_helpers::expect_true(
            "busy plan code",
            error.error_code == btrfsbackup::ErrorCode::RunnerProfileBusy,
            "busy plan returned the wrong error code"
        );
    }
    test_helpers::expect_true("busy plan preflight", fixture.preflight.calls == 0, "busy plan accessed the target");
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
    test_validate_only_emits_validation_lifecycle_without_backup_success();
    test_cancelled_run_does_not_persist_success();
    test_each_run_gets_a_fresh_cancellation_token();
    test_run_context_releases_resources_after_success();
    test_run_context_close_aggregates_cleanup_diagnostics();
    test_run_context_releases_resources_after_exception();
    test_success_ledger_failure_returns_degraded_success();
    test_terminal_status_failure_returns_degraded_success();
    test_target_cleanup_failure_prevents_success();
    test_preflight_failure_has_terminal_run_lifecycle();
    test_run_can_be_cancelled_during_preflight();
    test_executor_typed_failure_is_not_emitted_twice();
    test_busy_stops_before_target_access();
    test_plan_acquires_lease_and_defaults_to_offline_target();
    test_plan_can_explicitly_mount_target();
    test_plan_reports_target_cleanup_failure();
    test_busy_plan_stops_before_target_access();
    test_daily_match_skips_execution();
    test_cancel_validates_profile_and_writes_request();
    test_cancel_rejects_stale_run_when_lease_is_available();
    test_cancel_rejects_mismatched_run();
    return test_helpers::finish("backup service tests");
}
