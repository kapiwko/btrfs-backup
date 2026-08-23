#include <sys/stat.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <btrfsbackup/command/runner_command.hpp>
#include <btrfsbackup/config_fingerprint.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/run_state.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string action_name(btrfsbackup::BackupRunActionKind kind) {
    return std::to_string(static_cast<int>(kind));
}

class RecordingActionEffects final : public btrfsbackup::IBackupRunActionEffects {
public:
    std::vector<std::string> calls;
    std::vector<std::string> local_retention_deletes;
    std::vector<std::string> remote_retention_deletes;
    std::vector<std::string> recovered_pending_paths;
    btrfsbackup::BackupRunActionKind throw_on = btrfsbackup::BackupRunActionKind::SelectParent;
    bool should_throw = false;

    void execute_action(
        const btrfsbackup::BackupRunAction& action,
        const btrfsbackup::BackupSourceRunPlan& source_plan,
        const btrfsbackup::BackupRunPlan&
    ) override {
        calls.push_back(source_plan.source_id + ":" + action_name(action.kind));
        if (action.kind == btrfsbackup::BackupRunActionKind::ApplyLocalRetention) {
            for (const btrfsbackup::SnapshotInfo& snapshot : source_plan.local_retention.delete_snapshots) {
                local_retention_deletes.push_back(snapshot.path.string());
            }
        }
        if (action.kind == btrfsbackup::BackupRunActionKind::ApplyRemoteRetention) {
            for (const btrfsbackup::SnapshotInfo& snapshot : source_plan.remote_retention.delete_snapshots) {
                remote_retention_deletes.push_back(snapshot.path.string());
            }
        }
        if (action.kind == btrfsbackup::BackupRunActionKind::RecoverPending
            && source_plan.recovery.delete_local_snapshot) {
            recovered_pending_paths.push_back(source_plan.recovery.local_snapshot_path.string());
        }
        if (should_throw && action.kind == throw_on) {
            throw btrfsbackup::ValidationError("injected action failure: " + action_name(action.kind));
        }
    }
};

class ConfigurableTransferPipeline final : public btrfsbackup::ITransferPipeline {
public:
    std::vector<btrfsbackup::TransferPipelinePlan> plans;
    btrfsbackup::TransferResult next_result{
        .producer = {
            .started = true,
            .exit_code = 0,
        },
        .consumer = {
            .started = true,
            .exit_code = 0,
        },
        .bytes_transferred = 1024,
    };
    std::optional<fs::path> request_cancel_path;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan& plan,
        btrfsbackup::ITransferEventSink& events,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        plans.push_back(plan);
        events.on_transfer_event({
            .kind = btrfsbackup::TransferEventKind::Progress,
            .bytes_transferred = 1024,
            .bytes_produced = 1024,
            .delta_bytes = 1024,
            .elapsed_ms = 1000,
            .speed_bps = 1024,
        });
        if (request_cancel_path.has_value()) {
            btrfsbackup::write_cancel_request(*request_cancel_path);
            for (int i = 0; i < 20 && !cancellation.cancellation_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            next_result.cancelled = cancellation.cancellation_requested();
        }
        return next_result;
    }
};

class MetadataMap {
public:
    std::map<std::string, btrfsbackup::SnapshotMetadata> values;

    std::optional<btrfsbackup::SnapshotMetadata> read(const fs::path& path) const {
        auto found = values.find(path.string());
        if (found == values.end()) {
            return std::nullopt;
        }
        return found->second;
    }
};

void add_snapshot_metadata(
    MetadataMap& metadata,
    const fs::path& path,
    const std::string& uuid,
    const std::string& received_uuid = ""
) {
    metadata.values[path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = uuid,
        .received_uuid = received_uuid,
    };
}

btrfsbackup::Profile test_profile(const fs::path& root) {
    btrfsbackup::Profile profile;
    profile.id = "default";
    profile.name = "Default backup";
    profile.enabled = true;
    profile.target.device = "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555";
    profile.target.luks_uuid = "11111111-2222-3333-4444-555555555555";
    profile.target.btrfs_uuid = "22222222-3333-4444-5555-666666666666";
    profile.target.partition_uuid = "";
    profile.target.serial = "";
    profile.target.mapper_name = "backup";
    profile.target.mount_point = (root / "target").string();
    profile.target.mount_unit = "";
    profile.paths.sources_dir = (root / "config" / "sources.d").string();
    profile.paths.remote_root = (root / "target" / "snapshots").string();
    profile.paths.incoming_root = (root / "target" / ".incoming").string();
    profile.paths.state_dir = (root / "state").string();
    profile.paths.status_root = (root / "status").string();
    profile.paths.history_root = (root / "history").string();
    profile.settings.incremental_required = false;
    profile.settings.keep_failed_local_snapshot = false;
    profile.settings.remote_retention = 2;
    profile.settings.local_retention = 2;
    profile.notifications.enabled = false;
    profile.notifications.method = "none";
    profile.sources = {
        {
            .id = "root",
            .name = "System",
            .enabled = true,
            .subvolume = (root / "source" / "root").string(),
            .local_snapshot_dir = (root / "source" / ".snapshots" / "root").string(),
            .remote_subdir = "root",
            .remote_retention = 2,
            .local_retention = 2,
        },
    };
    return profile;
}

void add_home_source(btrfsbackup::Profile& profile, const fs::path& root) {
    profile.sources.push_back({
        .id = "home",
        .name = "Home",
        .enabled = true,
        .subvolume = (root / "source" / "home").string(),
        .local_snapshot_dir = (root / "source" / ".snapshots" / "home").string(),
        .remote_subdir = "home",
        .remote_retention = 2,
        .local_retention = 2,
    });
}

void write_profile(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    fs::path profile_path = config_root / "profiles" / profile.id / "profile.json";
    test_helpers::write_file(profile_path, btrfsbackup::profile_to_json(profile).dump(2));
    chmod(profile_path.c_str(), 0600);
}

void write_mountinfo(const fs::path& path, const btrfsbackup::Profile& profile) {
    std::string content;
    int mount_id = 21;
    for (const btrfsbackup::ProfileSource& source : profile.sources) {
        content += std::to_string(mount_id++) + " 31 0:20 / " + fs::path(source.subvolume).string() + " rw,relatime - btrfs /dev/source rw\n";
        content += std::to_string(mount_id++) + " 31 0:20 / " + fs::path(source.local_snapshot_dir).string() + " rw,relatime - btrfs /dev/source rw\n";
    }
    content += std::to_string(mount_id) + " 31 0:21 / " + fs::path(profile.target.mount_point).string() + " rw,relatime - btrfs /dev/mapper/backup rw\n";
    test_helpers::write_file(path, content);
}

std::string profile_fingerprint(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    return btrfsbackup::compute_config_fingerprint(
        "2.0.0",
        config_root / "profiles" / profile.id / "profile.json",
        {}
    );
}

void write_matching_last_success(const fs::path& config_root, const btrfsbackup::Profile& profile) {
    btrfsbackup::write_success_state(
        fs::path(profile.paths.state_dir) / "profiles" / profile.id,
        btrfsbackup::SuccessState{
            .date = "2026-08-23",
            .timestamp = "2026-08-23T08:00:00+02:00",
            .run_id = "20260823T080000Z-previous",
            .profile_id = profile.id,
            .profile_name = profile.name,
            .source_count = 1,
            .target_luks_uuid = profile.target.luks_uuid,
            .config_fingerprint = profile_fingerprint(config_root, profile),
        }
    );
}

void test_runner_plan_outputs_shadow_json() {
    fs::path root = test_helpers::test_root("runner-command", "plan");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "plan",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("runner result", std::to_string(result), "0");
    test_helpers::expect_eq("runner mode", json.at("mode").get<std::string>(), "shadow-plan");
    test_helpers::expect_eq("runner profile", json.at("profileId").get<std::string>(), "default");
    test_helpers::expect_eq("runner source count", std::to_string(json.at("sources").size()), "1");
    test_helpers::expect_eq(
        "runner planned snapshot",
        json.at("sources").at(0).at("localSnapshotPath").get<std::string>(),
        (root / "source" / ".snapshots" / "root" / "root-2026-08-23T080000Z").string()
    );
    test_helpers::expect_eq(
        "runner first action",
        json.at("sources").at(0).at("actions").at(0).at("kind").get<std::string>(),
        "cleanup-incoming"
    );

    fs::remove_all(root);
}

void test_runner_plan_validates_target_mount() {
    fs::path root = test_helpers::test_root("runner-command", "target-validation");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    std::ostringstream output;
    test_helpers::expect_validation_error("runner target uuid", [&] {
        (void)btrfsbackup::command::runner(
            config_root,
            {
                "plan",
                "--profile",
                "default",
                "--timestamp",
                "2026-08-23T080000Z",
                "--run-id",
                "20260823T080000Z-123-456",
                "--mountinfo",
                mountinfo.string(),
                "--mount-uuid",
                "/dev/source",
                "source-fs",
                "--mount-uuid",
                "/dev/mapper/backup",
                "99999999-9999-9999-9999-999999999999",
            },
            output
        );
    }, "Btrfs UUID mismatch");

    fs::remove_all(root);
}

void test_runner_execute_uses_injected_services_and_writes_state() {
    fs::path root = test_helpers::test_root("runner-command", "execute-injected");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("execute result", std::to_string(result), "0");
    test_helpers::expect_eq("execute mode", json.at("mode").get<std::string>(), "cpp-execute");
    test_helpers::expect_true("execute completed", json.at("completed").get<bool>(), "run should complete");
    test_helpers::expect_true("execute not skipped", !json.at("skipped").get<bool>(), "first run should execute");
    test_helpers::expect_eq("execute source count", std::to_string(json.at("sources").size()), "1");
    test_helpers::expect_true("execute full stream", !json.at("sources").at(0).at("incremental").get<bool>(), "first run should be full");
    test_helpers::expect_eq("execute transfer count", std::to_string(transfer_pipeline.plans.size()), "1");
    test_helpers::expect_true("execute effects", !action_effects.calls.empty(), "expected non-transfer effects");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true("checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint");
    test_helpers::expect_true("current exists", fs::is_regular_file(current), "missing current status");
    test_helpers::expect_true("history exists", fs::is_regular_file(history), "missing history");
    test_helpers::expect_true(
        "current succeeded",
        btrfsbackup::load_json_file(current).at("state") == "succeeded",
        "current status should be succeeded"
    );
    test_helpers::expect_true(
        "last success matches",
        btrfsbackup::last_success_matches(
            root / "state" / "profiles" / "default",
            "2026-08-23",
            profile.target.luks_uuid,
            profile_fingerprint(config_root, profile)
        ),
        "successful runner should write daily-limit state"
    );

    fs::remove_all(root);
}

void test_runner_execute_daily_limit_skips_matching_success() {
    fs::path root = test_helpers::test_root("runner-command", "execute-daily-limit-skip");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_matching_last_success(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--today",
            "2026-08-23",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("daily skip result", std::to_string(result), "0");
    test_helpers::expect_true("daily skip completed", json.at("completed").get<bool>(), "skip should be successful");
    test_helpers::expect_true("daily skip flag", json.at("skipped").get<bool>(), "matching success should skip");
    test_helpers::expect_eq("daily skip actions", std::to_string(action_effects.calls.size()), "0");
    test_helpers::expect_eq("daily skip transfers", std::to_string(transfer_pipeline.plans.size()), "0");

    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true("daily skip current exists", fs::is_regular_file(current), "missing skipped current status");
    test_helpers::expect_true("daily skip history exists", fs::is_regular_file(history), "missing skipped history");
    test_helpers::expect_true(
        "daily skip current",
        btrfsbackup::load_json_file(current).at("state") == "skipped",
        "current status should be skipped"
    );
    test_helpers::expect_true(
        "daily skip history",
        btrfsbackup::load_json_file(history).at("state") == "skipped",
        "history should be skipped"
    );

    fs::remove_all(root);
}

void test_runner_execute_force_ignores_daily_limit() {
    fs::path root = test_helpers::test_root("runner-command", "execute-daily-limit-force");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_matching_last_success(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--force",
            "--profile",
            "default",
            "--today",
            "2026-08-23",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("force result", std::to_string(result), "0");
    test_helpers::expect_true("force completed", json.at("completed").get<bool>(), "forced run should complete");
    test_helpers::expect_true("force not skipped", !json.at("skipped").get<bool>(), "forced run should bypass daily limit");
    test_helpers::expect_eq("force transfers", std::to_string(transfer_pipeline.plans.size()), "1");
    test_helpers::expect_true("force actions", !action_effects.calls.empty(), "forced run should execute actions");

    fs::remove_all(root);
}

void test_runner_execute_validate_builds_plan_without_effects() {
    fs::path root = test_helpers::test_root("runner-command", "execute-validate");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--validate",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("validate result", std::to_string(result), "0");
    test_helpers::expect_eq("validate mode", json.at("mode").get<std::string>(), "cpp-validate");
    test_helpers::expect_true("validate completed", json.at("completed").get<bool>(), "validation should complete");
    test_helpers::expect_eq("validate transfer count", std::to_string(transfer_pipeline.plans.size()), "0");
    test_helpers::expect_true("validate effects", action_effects.calls.empty(), "validation should not execute actions");
    test_helpers::expect_true("validate checkpoint absent", !fs::exists(root / "state" / "profiles" / "default" / "checkpoint.json"), "validation should not write checkpoint");

    fs::remove_all(root);
}

void test_runner_execute_transfer_failure_writes_failed_status() {
    fs::path root = test_helpers::test_root("runner-command", "execute-transfer-failure");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    transfer_pipeline.next_result.producer.exit_code = 7;
    transfer_pipeline.next_result.producer.diagnostics = "send failed";
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    test_helpers::expect_validation_error("execute transfer failure", [&] {
        (void)btrfsbackup::command::runner(
            config_root,
            {
                "execute",
                "--profile",
                "default",
                "--timestamp",
                "2026-08-23T080000Z",
                "--run-id",
                "20260823T080000Z-123-456",
                "--mountinfo",
                mountinfo.string(),
                "--mount-uuid",
                "/dev/source",
                "source-fs",
                "--mount-uuid",
                "/dev/mapper/backup",
                profile.target.btrfs_uuid,
            },
            output,
            &services
        );
    }, "producer failed with exit code 7");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    btrfsbackup::Json current_json = btrfsbackup::load_json_file(current);
    test_helpers::expect_true("failed checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint before failure");
    test_helpers::expect_true(
        "failed checkpoint action",
        btrfsbackup::load_json_file(checkpoint).at("action") == "create-snapshot",
        "failed send-receive should not be checkpointed"
    );
    test_helpers::expect_true(
        "failed current",
        current_json.at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_eq("failed transfer error code", current_json.at("errorCode").get<std::string>(), "transfer.producer_failed");
    test_helpers::expect_true(
        "failed history",
        btrfsbackup::load_json_file(history).at("state") == "failed",
        "history should fail"
    );

    fs::remove_all(root);
}

void test_runner_execute_commit_failure_writes_failed_status() {
    fs::path root = test_helpers::test_root("runner-command", "execute-commit-failure");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    action_effects.should_throw = true;
    action_effects.throw_on = btrfsbackup::BackupRunActionKind::CommitReceived;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    test_helpers::expect_validation_error("execute commit failure", [&] {
        (void)btrfsbackup::command::runner(
            config_root,
            {
                "execute",
                "--profile",
                "default",
                "--timestamp",
                "2026-08-23T080000Z",
                "--run-id",
                "20260823T080000Z-123-456",
                "--mountinfo",
                mountinfo.string(),
                "--mount-uuid",
                "/dev/source",
                "source-fs",
                "--mount-uuid",
                "/dev/mapper/backup",
                profile.target.btrfs_uuid,
            },
            output,
            &services
        );
    }, "injected action failure");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true(
        "commit failure checkpoint action",
        btrfsbackup::load_json_file(checkpoint).at("action") == "verify-received",
        "failed commit should not be checkpointed"
    );
    test_helpers::expect_true(
        "commit failure current",
        btrfsbackup::load_json_file(current).at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_true(
        "commit failure phase",
        btrfsbackup::load_json_file(history).at("phase") == "commit-received",
        "history should identify commit phase"
    );

    fs::remove_all(root);
}

void test_runner_execute_verify_failure_writes_failed_status() {
    fs::path root = test_helpers::test_root("runner-command", "execute-verify-failure");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    action_effects.should_throw = true;
    action_effects.throw_on = btrfsbackup::BackupRunActionKind::VerifyReceived;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    test_helpers::expect_validation_error("execute verify failure", [&] {
        (void)btrfsbackup::command::runner(
            config_root,
            {
                "execute",
                "--profile",
                "default",
                "--timestamp",
                "2026-08-23T080000Z",
                "--run-id",
                "20260823T080000Z-123-456",
                "--mountinfo",
                mountinfo.string(),
                "--mount-uuid",
                "/dev/source",
                "source-fs",
                "--mount-uuid",
                "/dev/mapper/backup",
                profile.target.btrfs_uuid,
            },
            output,
            &services
        );
    }, "injected action failure");

    fs::path checkpoint = root / "state" / "profiles" / "default" / "checkpoint.json";
    fs::path current = root / "status" / "default" / "current.json";
    fs::path history = root / "history" / "default" / "20260823T080000Z-123-456.json";
    test_helpers::expect_true(
        "verify failure checkpoint action",
        btrfsbackup::load_json_file(checkpoint).at("action") == "send-receive",
        "failed verify should not be checkpointed"
    );
    test_helpers::expect_true(
        "verify failure current",
        btrfsbackup::load_json_file(current).at("state") == "failed",
        "current status should fail"
    );
    test_helpers::expect_true(
        "verify failure phase",
        btrfsbackup::load_json_file(history).at("phase") == "verify-received",
        "history should identify verify phase"
    );

    fs::remove_all(root);
}

void test_runner_execute_multi_source_success() {
    fs::path root = test_helpers::test_root("runner-command", "execute-multi-source");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / "home");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "home");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "home");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    add_home_source(profile, root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("multi execute result", std::to_string(result), "0");
    test_helpers::expect_true("multi completed", json.at("completed").get<bool>(), "run should complete");
    test_helpers::expect_eq("multi transfers", std::to_string(transfer_pipeline.plans.size()), "2");
    test_helpers::expect_true("root actions", std::find(
        action_effects.calls.begin(),
        action_effects.calls.end(),
        "root:" + action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot)
    ) != action_effects.calls.end(), "missing root create action");
    test_helpers::expect_true("home actions", std::find(
        action_effects.calls.begin(),
        action_effects.calls.end(),
        "home:" + action_name(btrfsbackup::BackupRunActionKind::CreateSnapshot)
    ) != action_effects.calls.end(), "missing home create action");

    btrfsbackup::Json current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_true("multi status", current.at("state") == "succeeded", "status should succeed");
    test_helpers::expect_true("multi source count", current.at("sourceCount") == 2, "status should include both sources");

    fs::remove_all(root);
}

void test_runner_execute_incremental_uses_selected_parent() {
    fs::path root = test_helpers::test_root("runner-command", "execute-incremental");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path local_parent = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::path remote_parent = root / "target" / "snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(local_parent);
    fs::create_directories(remote_parent);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    MetadataMap metadata;
    add_snapshot_metadata(metadata, local_parent, "parent-uuid");
    add_snapshot_metadata(metadata, remote_parent, "remote-parent-uuid", "parent-uuid");

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
        .snapshot_metadata_reader = [&metadata](const fs::path& path) {
            return metadata.read(path);
        },
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    test_helpers::expect_eq("incremental result", std::to_string(result), "0");
    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_true("incremental output", json.at("sources").at(0).at("incremental").get<bool>(), "runner output should identify incremental transfer");
    test_helpers::expect_eq("incremental output parent", json.at("sources").at(0).at("parentPath").get<std::string>(), local_parent.string());
    test_helpers::expect_eq("incremental transfers", std::to_string(transfer_pipeline.plans.size()), "1");
    const std::vector<std::string>& send_argv = transfer_pipeline.plans.at(0).producer_argv;
    test_helpers::expect_eq("incremental parent flag", send_argv.at(2), "-p");
    test_helpers::expect_eq("incremental parent path", send_argv.at(3), local_parent.string());

    fs::remove_all(root);
}

void test_runner_execute_retention_plans_local_and_remote_deletes() {
    fs::path root = test_helpers::test_root("runner-command", "execute-retention");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    profile.sources.at(0).local_retention = 2;
    profile.sources.at(0).remote_retention = 2;
    fs::path local_old = root / "source" / ".snapshots" / "root" / "root-2026-08-20T080000Z";
    fs::path local_keep = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::path remote_old = root / "target" / "snapshots" / "root" / "root-2026-08-20T080000Z";
    fs::path remote_keep = root / "target" / "snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(local_old);
    fs::create_directories(local_keep);
    fs::create_directories(remote_old);
    fs::create_directories(remote_keep);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    MetadataMap metadata;
    add_snapshot_metadata(metadata, local_old, "local-old-uuid");
    add_snapshot_metadata(metadata, local_keep, "local-keep-uuid");
    add_snapshot_metadata(metadata, remote_old, "remote-old-uuid", "local-old-uuid");
    add_snapshot_metadata(metadata, remote_keep, "remote-keep-uuid", "local-keep-uuid");

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
        .snapshot_metadata_reader = [&metadata](const fs::path& path) {
            return metadata.read(path);
        },
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    test_helpers::expect_eq("retention result", std::to_string(result), "0");
    test_helpers::expect_true("remote retention action", std::find(
        action_effects.calls.begin(),
        action_effects.calls.end(),
        "root:" + action_name(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention)
    ) != action_effects.calls.end(), "missing remote retention action");
    test_helpers::expect_true("local retention action", std::find(
        action_effects.calls.begin(),
        action_effects.calls.end(),
        "root:" + action_name(btrfsbackup::BackupRunActionKind::ApplyLocalRetention)
    ) != action_effects.calls.end(), "missing local retention action");
    test_helpers::expect_eq("remote retention delete count", std::to_string(action_effects.remote_retention_deletes.size()), "1");
    test_helpers::expect_eq("remote retention delete", action_effects.remote_retention_deletes.at(0), remote_old.string());
    test_helpers::expect_eq("local retention delete count", std::to_string(action_effects.local_retention_deletes.size()), "1");
    test_helpers::expect_eq("local retention delete", action_effects.local_retention_deletes.at(0), local_old.string());

    fs::remove_all(root);
}

void test_runner_execute_pending_recovery_deletes_orphan() {
    fs::path root = test_helpers::test_root("runner-command", "execute-pending-recovery");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path pending = root / "source" / ".snapshots" / "root" / "root-2026-08-22T080000Z";
    fs::create_directories(pending);

    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);
    btrfsbackup::write_pending_marker(
        root / "state" / "profiles" / "default",
        btrfsbackup::PendingMarker{
            .source_name = "root",
            .local_snapshot_path = pending.string(),
            .run_id = "20260822T080000Z-123-456",
            .timestamp = "2026-08-22T08:00:00Z",
        }
    );

    MetadataMap metadata;
    add_snapshot_metadata(metadata, pending, "orphan-uuid");

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
        .snapshot_metadata_reader = [&metadata](const fs::path& path) {
            return metadata.read(path);
        },
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    test_helpers::expect_eq("pending recovery result", std::to_string(result), "0");
    test_helpers::expect_true("recover action first", !action_effects.calls.empty(), "expected action calls");
    test_helpers::expect_eq(
        "recover first action",
        action_effects.calls.at(0),
        "root:" + action_name(btrfsbackup::BackupRunActionKind::RecoverPending)
    );
    test_helpers::expect_eq("pending delete count", std::to_string(action_effects.recovered_pending_paths.size()), "1");
    test_helpers::expect_eq("pending delete path", action_effects.recovered_pending_paths.at(0), pending.string());

    fs::remove_all(root);
}

void test_runner_cancel_writes_cancel_request_without_target_mount() {
    fs::path root = test_helpers::test_root("runner-command", "cancel");
    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    write_profile(config_root, profile);

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "cancel",
            "--profile",
            "default",
        },
        output
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    test_helpers::expect_eq("cancel result", std::to_string(result), "0");
    test_helpers::expect_eq("cancel mode", json.at("mode").get<std::string>(), "cpp-cancel");
    test_helpers::expect_true("cancel requested json", json.at("cancelRequested").get<bool>(), "cancel should be requested");
    test_helpers::expect_true(
        "cancel request exists",
        btrfsbackup::cancel_requested(profile_state_dir),
        "cancel request file missing"
    );

    fs::remove_all(root);
}

void test_runner_execute_honors_cancel_request_during_transfer() {
    fs::path root = test_helpers::test_root("runner-command", "execute-cancel");
    fs::create_directories(root / "source" / "root");
    fs::create_directories(root / "source" / ".snapshots" / "root");
    fs::create_directories(root / "target" / "snapshots" / "root");
    fs::create_directories(root / "target" / ".incoming");

    btrfsbackup::Profile profile = test_profile(root);
    fs::path config_root = root / "config";
    fs::path mountinfo = root / "mountinfo";
    write_profile(config_root, profile);
    write_mountinfo(mountinfo, profile);

    RecordingActionEffects action_effects;
    ConfigurableTransferPipeline transfer_pipeline;
    fs::path profile_state_dir = root / "state" / "profiles" / "default";
    transfer_pipeline.request_cancel_path = profile_state_dir;
    btrfsbackup::command::RunnerExecutionServices services{
        .action_effects = action_effects,
        .transfer_pipeline = transfer_pipeline,
    };

    std::ostringstream output;
    int result = btrfsbackup::command::runner(
        config_root,
        {
            "execute",
            "--profile",
            "default",
            "--timestamp",
            "2026-08-23T080000Z",
            "--run-id",
            "20260823T080000Z-123-456",
            "--mountinfo",
            mountinfo.string(),
            "--mount-uuid",
            "/dev/source",
            "source-fs",
            "--mount-uuid",
            "/dev/mapper/backup",
            profile.target.btrfs_uuid,
        },
        output,
        &services
    );

    btrfsbackup::Json json = btrfsbackup::Json::parse(output.str());
    test_helpers::expect_eq("execute cancel result", std::to_string(result), "1");
    test_helpers::expect_true("execute cancel incomplete", !json.at("completed").get<bool>(), "cancelled run should not complete");
    test_helpers::expect_true("execute cancel flag", json.at("cancelled").get<bool>(), "cancelled flag should be true");

    fs::path current = root / "status" / "default" / "current.json";
    btrfsbackup::Json current_json = btrfsbackup::load_json_file(current);
    test_helpers::expect_eq("execute cancel status state", current_json.at("state").get<std::string>(), "cancelled");
    test_helpers::expect_eq("execute cancel code", current_json.at("errorCode").get<std::string>(), "runner.cancelled");
    test_helpers::expect_true(
        "execute cancel request cleaned",
        !btrfsbackup::cancel_requested(profile_state_dir),
        "handled cancellation request should be removed"
    );

    fs::remove_all(root);
}

} // namespace

int main() {
    test_runner_plan_outputs_shadow_json();
    test_runner_plan_validates_target_mount();
    test_runner_execute_uses_injected_services_and_writes_state();
    test_runner_execute_daily_limit_skips_matching_success();
    test_runner_execute_force_ignores_daily_limit();
    test_runner_execute_validate_builds_plan_without_effects();
    test_runner_execute_transfer_failure_writes_failed_status();
    test_runner_execute_commit_failure_writes_failed_status();
    test_runner_execute_verify_failure_writes_failed_status();
    test_runner_execute_multi_source_success();
    test_runner_execute_incremental_uses_selected_parent();
    test_runner_execute_retention_plans_local_and_remote_deletes();
    test_runner_execute_pending_recovery_deletes_orphan();
    test_runner_cancel_writes_cancel_request_without_target_mount();
    test_runner_execute_honors_cancel_request_during_transfer();

    return test_helpers::finish("runner command tests");
}
