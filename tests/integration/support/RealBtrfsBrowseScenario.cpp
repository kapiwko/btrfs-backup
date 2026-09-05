// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealBtrfsTestEnvironment.hpp"

#include <stdexcept>
#include <system_error>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;

void RealBtrfsTestEnvironment::require_browse_session() const {
    constexpr std::string_view user = "btrfs-raii-test";
    const std::string root_name = root_.filename().string();
    const std::string suffix = root_name.substr(root_name.size() - 6U);
    const std::string unit_stem = "btrfs-backup-raii-manager-" + suffix;
    const std::string unit = unit_stem + ".service";
    const fs::path policy = "/etc/polkit-1/rules.d/49-btrfs-backup-raii.rules";
    const fs::path probe = target_mount_ / "snapshots/browse-probe.txt";
    const fs::path manager = backupctl_.parent_path() / "btrfs-backupd";
    if (!fs::is_regular_file(manager) || fs::exists(policy) || fs::exists(probe))
        throw std::runtime_error("browse fixture prerequisites are unsafe");
    if (command({"id", "-u", std::string(user)}).status == 0)
        throw std::runtime_error("browse fixture user already exists");
    if (command({"busctl", "--system", "status", "io.github.btrfsbackup.Manager1"}).status == 0)
        throw std::runtime_error("manager D-Bus name is already owned");

    bool user_created = false;
    bool manager_started = false;
    const bool polkit_was_active =
        command({"systemctl", "is-active", "--quiet", "polkit.service"}).status == 0;
    const auto cleanup = [&] {
        std::vector<std::string> errors;
        const auto run_cleanup = [&](std::vector<std::string> arguments, std::string_view operation) {
            try {
                const auto result = command(std::move(arguments), std::chrono::seconds(30));
                if (result.status != 0)
                    errors.push_back(std::string(operation) + ": " + command_diagnostic(result));
            } catch (const std::exception& error) {
                errors.push_back(std::string(operation) + ": " + error.what());
            }
        };
        if (manager_started)
            run_cleanup({"systemctl", "stop", unit}, "stop isolated browse manager");
        std::error_code ignored;
        fs::remove(policy, ignored);
        if (ignored)
            errors.push_back("remove browse polkit rule: " + ignored.message());
        if (polkit_was_active)
            run_cleanup({"systemctl", "restart", "polkit.service"}, "reload original polkit rules");
        else
            run_cleanup({"systemctl", "stop", "polkit.service"}, "stop browse polkit authority");
        fs::remove(probe, ignored);
        if (ignored)
            errors.push_back("remove browse probe: " + ignored.message());
        if (user_created)
            run_cleanup({"userdel", std::string(user)}, "remove browse test user");
        return errors;
    };

    try {
        auto created = command(
            {"useradd", "--system", "--no-create-home", "--shell", "/usr/bin/nologin", std::string(user)}
        );
        if (created.status != 0)
            throw std::runtime_error("cannot create browse test user: " + command_diagnostic(created));
        user_created = true;
        const auto uid = command({"id", "-u", std::string(user)});
        if (uid.status != 0 || trim_output(uid.output) == "0")
            throw std::runtime_error("browse test caller is not an unprivileged user");
        require_command({"systemctl", "restart", "polkit.service"}, "start browse polkit authority");
        fs::create_directories(root_ / "public-profiles");
        write_test_file(probe, "browse probe\n");
        auto started = command(
            {"systemd-run",
             "--unit=" + unit_stem,
             "--collect",
             "--property=Type=dbus",
             "--property=BusName=io.github.btrfsbackup.Manager1",
             manager.string(),
             "--config-root",
             config_root_.string(),
             "--public-profile-root",
             (root_ / "public-profiles").string(),
             "--status-root",
             status_root_.string(),
             "--history-root",
             history_root_.string(),
             "--target-mount-root",
             target_mount_root_.string(),
             "--audit-log",
             (root_ / "manager-audit.jsonl").string(),
             "--skip-configuration-activation"},
            std::chrono::seconds(30)
        );
        if (started.status != 0)
            throw std::runtime_error("cannot start isolated browse manager: " + command_diagnostic(started));
        manager_started = true;
        require_command({"systemctl", "is-active", "--quiet", unit}, "verify isolated browse manager");
        const auto denied = command(
            {"runuser", "-u", std::string(user), "--", browse_session_client_.string(), "raii", "--expect-denied"},
            std::chrono::seconds(30)
        );
        if (denied.status != 0)
            throw std::runtime_error("unprivileged browse session was not denied: " + command_diagnostic(denied));
        write_test_file(
            policy,
            "polkit.addRule(function(action, subject) {\n"
            "  if (action.id == \"io.github.btrfsbackup.open-browse-session\" &&\n"
            "      subject.user == \"btrfs-raii-test\") return polkit.Result.YES;\n"
            "});\n"
        );
        require_command({"chmod", "0644", policy.string()}, "protect browse polkit rule");
        require_command({"systemctl", "restart", "polkit.service"}, "reload browse polkit rule");
        const auto client = command(
            {"runuser",
             "-u",
             std::string(user),
             "--",
             browse_session_client_.string(),
             "raii",
             "--self-check",
             target_mount_.string()},
            std::chrono::seconds(30)
        );
        if (client.status != 0)
            throw std::runtime_error("unprivileged browse session failed: " + command_diagnostic(client));
        require_command({"findmnt", "--mountpoint", target_mount_.string()}, "verify target after browse cleanup");
    } catch (const std::exception& error) {
        const std::string primary = error.what();
        const auto cleanup_errors = cleanup();
        if (!cleanup_errors.empty())
            throw std::runtime_error(primary + "; browse cleanup failed: " + join_test_errors(cleanup_errors));
        throw;
    } catch (...) {
        const auto cleanup_errors = cleanup();
        if (!cleanup_errors.empty())
            throw std::runtime_error("browse scenario and cleanup failed: " + join_test_errors(cleanup_errors));
        throw;
    }
    const auto cleanup_errors = cleanup();
    if (!cleanup_errors.empty())
        throw std::runtime_error("browse cleanup failed: " + join_test_errors(cleanup_errors));
}

} // namespace btrfsbackup::integration
