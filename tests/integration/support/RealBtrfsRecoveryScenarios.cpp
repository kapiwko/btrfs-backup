// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealBtrfsTestEnvironment.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

void RealBtrfsTestEnvironment::require_target_identity_rejected() const {
    const fs::path profile_path = config_root_ / "profiles/raii/profile.json";
    std::ifstream profile_input(profile_path);
    Json profile = Json::parse(profile_input);
    profile_input.close();
    if (!profile_input)
        throw std::runtime_error("cannot read profile before target identity test");
    const std::string original_profile = profile.dump() + "\n";
    profile["target"]["btrfsUuid"] = "00000000-0000-0000-0000-000000000000";
    write_test_file(profile_path, profile.dump() + "\n");
    const std::size_t local_before = local_snapshot_count();
    const std::size_t remote_before = remote_snapshot_count();
    CommandResult result;
    try {
        result = execute_backup("2026-08-22T09:00:00Z", "20260822T090000Z-raii-wrong-target");
    } catch (...) {
        write_test_file(profile_path, original_profile);
        throw;
    }
    write_test_file(profile_path, original_profile);
    if (result.status == 0 || !command_diagnostic(result).contains("Btrfs UUID mismatch"))
        throw std::runtime_error("runner did not reject a mismatched target identity: " + command_diagnostic(result));
    if (local_snapshot_count() != local_before || remote_snapshot_count() != remote_before)
        throw std::runtime_error("target identity rejection changed snapshot state");
}

void RealBtrfsTestEnvironment::write_pending_marker(
    const fs::path& local_snapshot,
    const fs::path& final_snapshot,
    std::string_view run_id
) const {
    const fs::path marker = state_root_ / "profiles/raii/pending-home";
    write_test_file(
        marker,
        "source_name=home\nlocal_snapshot_path=" + local_snapshot.string() +
            "\nfinal_snapshot_path=" + final_snapshot.string() + "\nrun_id=" + std::string(run_id) +
            "\ntimestamp=2026-08-23T08:00:00Z\n"
    );
    require_command({"chmod", "0600", marker.string()}, "protect pending marker");
    require_command({"sync", "-f", marker.string()}, "sync pending marker");
}

void RealBtrfsTestEnvironment::require_pre_receive_recovery() {
    const fs::path orphan = source_mount_ / ".snapshots/home/home-2026-08-19T080000Z";
    const fs::path final = target_mount_ / "snapshots/home" / orphan.filename();
    require_command(
        {"btrfs", "subvolume", "snapshot", "-r", (source_mount_ / "home").string(), orphan.string()},
        "create pre-receive recovery orphan"
    );
    write_pending_marker(orphan, final, "20260819T080000Z-raii-interrupted");
    const auto result = execute_backup("2026-08-23T08:00:00Z", "20260823T080000Z-raii-recovery-before");
    if (result.status != 0)
        throw std::runtime_error("pre-receive recovery backup failed: " + command_diagnostic(result));
    if (fs::exists(orphan) || fs::exists(state_root_ / "profiles/raii/pending-home"))
        throw std::runtime_error("pre-receive recovery retained its orphan or pending marker");
    if (!incoming_is_empty())
        throw std::runtime_error("pre-receive recovery retained incoming artifacts");
}

void RealBtrfsTestEnvironment::require_post_commit_recovery() {
    const fs::path local = latest_snapshot(source_mount_ / ".snapshots/home");
    const fs::path remote = latest_snapshot(target_mount_ / "snapshots/home");
    write_pending_marker(local, remote, "20260823T080000Z-raii-committed");
    write_source_file("post-commit-recovery.txt", "recovery generation\n");
    const auto result = execute_backup("2026-08-24T08:00:00Z", "20260824T080000Z-raii-recovery-after");
    if (result.status != 0)
        throw std::runtime_error("post-commit recovery backup failed: " + command_diagnostic(result));
    if (fs::exists(state_root_ / "profiles/raii/pending-home"))
        throw std::runtime_error("post-commit recovery retained its pending marker");
    if (!fs::is_directory(local) || !fs::is_directory(remote))
        throw std::runtime_error("post-commit recovery removed a committed snapshot pair");
    require_latest_snapshots_match();
    if (!incoming_is_empty())
        throw std::runtime_error("post-commit recovery retained incoming artifacts");
}

std::string RealBtrfsTestEnvironment::subvolume_field(
    const fs::path& subvolume,
    std::string_view field
) const {
    const auto metadata = command({"btrfs", "subvolume", "show", subvolume.string()});
    if (metadata.status != 0)
        throw std::runtime_error("cannot inspect subvolume " + subvolume.string() + ": " + command_diagnostic(metadata));
    std::istringstream lines(metadata.output);
    std::string line;
    const std::string prefix = std::string(field) + ':';
    while (std::getline(lines, line)) {
        line = trim_output(std::move(line));
        if (line.starts_with(prefix))
            return trim_output(line.substr(prefix.size()));
    }
    throw std::runtime_error("subvolume metadata omitted " + std::string(field));
}

void RealBtrfsTestEnvironment::require_restore_scenarios() const {
    const fs::path repository = target_mount_ / "snapshots";
    const fs::path latest_remote = latest_snapshot(repository / "home");

    const fs::path raw_root = source_mount_ / "restore-drill";
    const fs::path raw_stream = root_ / "restore-stream.btrfs";
    const fs::path raw_restored = raw_root / latest_remote.filename();
    fs::create_directories(raw_root);
    require_command({"btrfs", "send", "-f", raw_stream.string(), latest_remote.string()}, "write restore stream");
    require_command({"btrfs", "receive", "-f", raw_stream.string(), raw_root.string()}, "receive restore stream");
    require_command({"diff", "-qr", latest_remote.string(), raw_restored.string()}, "compare raw restore");
    require_command({"btrfs", "subvolume", "delete", raw_restored.string()}, "delete raw restored subvolume");
    const bool removed_raw_stream = fs::remove(raw_stream);
    const bool removed_raw_root = fs::remove(raw_root);
    if (!removed_raw_stream || !removed_raw_root)
        throw std::runtime_error("raw restore cleanup left artifacts");

    const std::string created_at = "2026-08-24T09:00:00Z";
    const std::string relative = "home/" + latest_remote.filename().string();
    const std::string snapshot_uuid = subvolume_field(latest_remote, "UUID");
    const std::string received_uuid = subvolume_field(latest_remote, "Received UUID");
    const auto target_uuid_result = command({"blkid", "-s", "UUID", "-o", "value", mapper_path_.string()});
    const std::string target_uuid = trim_output(target_uuid_result.output);
    if (target_uuid_result.status != 0 || target_uuid.empty())
        throw std::runtime_error("cannot read restore target UUID: " + command_diagnostic(target_uuid_result));
    write_test_file(
        repository / "repository.json",
        Json({{"schemaVersion", 1},
              {"repositoryId", "real-" + target_uuid},
              {"targetFilesystemUuid", target_uuid},
              {"createdAt", created_at},
              {"features", Json::array({"catalog-v1"})}})
                .dump() +
            "\n"
    );
    write_test_file(
        repository / "catalog.json",
        Json({{"schemaVersion", 1},
              {"generation", 1},
              {"snapshots",
               Json::array({{{"snapshotId", "real-latest"}, {"hostId", "real-host"}, {"profileId", "raii"}, {"sourceId", "home"}, {"relativePath", relative}, {"createdAt", created_at}, {"uuid", snapshot_uuid}, {"receivedUuid", received_uuid}, {"verified", true}}})}})
                .dump() +
            "\n"
    );

    const fs::path restored = source_mount_ / "restore-engine-result";
    const fs::path drill_root = source_mount_ / "restore-engine-drill";
    const fs::path drill_destination = drill_root / "result";
    const auto restore_command = [&](std::string_view operation, const fs::path& destination, std::string_view transaction) {
        std::vector<std::string> arguments{backupctl_.string(), "restore", std::string(operation), "--repository", repository.string(), "--snapshot", "real-latest", "--source", ".", "--destination", destination.string(), "--transaction", std::string(transaction)};
        if (operation != "drill")
            arguments.emplace_back("--subvolume");
        return command(std::move(arguments), std::chrono::seconds(60));
    };
    auto plan = restore_command("plan", restored, "real-plan");
    if (plan.status != 0 || fs::exists(restored))
        throw std::runtime_error("restore plan failed or mutated its destination: " + command_diagnostic(plan));
    auto restored_result = restore_command("execute", restored, "real-execute");
    if (restored_result.status != 0)
        throw std::runtime_error("restore execute failed: " + command_diagnostic(restored_result));
    require_command({"btrfs", "subvolume", "show", restored.string()}, "inspect restored subvolume");
    require_command({"diff", "-qr", latest_remote.string(), restored.string()}, "compare restored subvolume");
    auto drill = restore_command("drill", drill_destination, "real-drill");
    if (drill.status != 0 || fs::exists(drill_destination) ||
        fs::exists(drill_root / ".btrfs-backup-restore-real-drill.staging"))
        throw std::runtime_error("restore drill failed or retained artifacts: " + command_diagnostic(drill));

    require_command({"btrfs", "subvolume", "delete", restored.string()}, "delete restored subvolume");
    const bool removed_drill_root = fs::remove(drill_root);
    const bool removed_catalog = fs::remove(repository / "catalog.json");
    const bool removed_repository = fs::remove(repository / "repository.json");
    if (!removed_drill_root || !removed_catalog || !removed_repository)
        throw std::runtime_error("public restore cleanup left artifacts");
}

void RealBtrfsTestEnvironment::require_public_cancellation() const {
    const fs::path profile_path = config_root_ / "profiles/raii/profile.json";
    std::ifstream profile_input(profile_path);
    Json profile = Json::parse(profile_input);
    profile_input.close();
    if (!profile_input)
        throw std::runtime_error("cannot read profile before cancellation test");
    const std::string original_profile = profile.dump() + "\n";
    const fs::path last_success = state_root_ / "profiles/raii/last-success";
    const auto read_document = [](const fs::path& path) {
        std::ifstream input(path);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const std::string success_before = read_document(last_success);
    const std::size_t local_before = local_snapshot_count();
    const std::size_t remote_before = remote_snapshot_count();
    const fs::path hook = fs::path("/etc/btrfs-backup/hooks.d") /
        ("raii-cancel-" + root_.filename().string());
    if (fs::exists(hook))
        throw std::runtime_error("cancellation hook path already exists: " + hook.string());
    fs::copy_file(backupctl_, hook, fs::copy_options::none);
    fs::permissions(
        hook,
        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
            fs::perms::others_read | fs::perms::others_exec,
        fs::perm_options::replace
    );
    const std::string run_id = "20260824T100000Z-raii-cancel";
    profile["hooks"]["beforeSnapshot"] = Json::array({{{"type", "program"}, {"program", hook.string()}, {"arguments", Json::array({"--profile-dir", config_root_.string(), "--status-root", status_root_.string(), "--history-root", history_root_.string(), "runner", "cancel", "--profile", "raii", "--run-id", run_id})}, {"timeoutSeconds", 30}}});
    write_test_file(profile_path, profile.dump() + "\n");

    CommandResult result;
    try {
        result = execute_backup("2026-08-24T10:00:00Z", run_id);
        write_test_file(profile_path, original_profile);
        if (!fs::remove(hook))
            throw std::runtime_error("cancellation hook disappeared before cleanup");
    } catch (...) {
        std::error_code ignored;
        fs::remove(hook, ignored);
        write_test_file(profile_path, original_profile);
        throw;
    }
    if (result.status != 1)
        throw std::runtime_error("cancelled runner returned an unexpected status: " + command_diagnostic(result));
    const Json response = Json::parse(result.output);
    if (response.at("completed").get<bool>() || !response.at("cancelled").get<bool>())
        throw std::runtime_error("runner did not report cancellation");
    std::ifstream status_input(status_root_ / "raii/current.json");
    const Json current = Json::parse(status_input);
    if (current.at("state") != "cancelled" || current.at("errorCode") != "backup.cancelled")
        throw std::runtime_error("cancelled runner persisted an invalid terminal status");
    if (fs::exists(state_root_ / "profiles/raii/cancel-request"))
        throw std::runtime_error("runner retained a handled cancellation request");
    if (read_document(last_success) != success_before || local_snapshot_count() != local_before ||
        remote_snapshot_count() != remote_before)
        throw std::runtime_error("cancelled runner changed successful snapshot state");
    if (fs::exists(state_root_ / "profiles/raii/pending-home") || !incoming_is_empty())
        throw std::runtime_error("cancelled pre-snapshot runner retained transactional artifacts");
}

} // namespace btrfsbackup::integration
