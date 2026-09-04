// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealInstalledRuntimeTest.hpp"

#include "IntegrationTestProcess.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;

namespace {

constexpr auto command_timeout = std::chrono::seconds(120);
constexpr std::string_view profile_id = "default";
const fs::path profile_path = "/etc/btrfs-backup/profiles/default/profile.json";

[[nodiscard]] CommandResult command(std::vector<std::string> arguments) {
    return run_test_process(std::move(arguments), command_timeout);
}

[[nodiscard]] std::string require_output(
    std::vector<std::string> arguments,
    std::string_view operation
) {
    const auto result = command(std::move(arguments));
    const std::string output = trim_output(result.output);
    if (result.status != 0 || output.empty())
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
    return output;
}

} // namespace

RealInstalledRuntimeTest::RealInstalledRuntimeTest(
    fs::path backupctl,
    fs::path runtime,
    fs::path test_root,
    fs::path source_mount,
    fs::path target_mount,
    fs::path target_device,
    std::string mapper_name,
    fs::path passphrase_file
)
    : backupctl_(fs::canonical(backupctl)),
      runtime_(fs::canonical(runtime)),
      rendered_root_(fs::canonical(test_root) / "rendered"),
      source_mount_(fs::canonical(source_mount)),
      target_mount_(fs::canonical(target_mount)),
      target_device_(fs::canonical(target_device)),
      mapper_name_(std::move(mapper_name)),
      mapper_path_(fs::path("/dev/mapper") / mapper_name_),
      passphrase_file_(fs::canonical(passphrase_file)) {
    if (mapper_name_.empty() || mapper_name_.contains('/'))
        throw std::runtime_error("invalid integration mapper name");
}

void RealInstalledRuntimeTest::require_success(
    std::vector<std::string> arguments,
    std::string_view operation
) const {
    const auto result = command(std::move(arguments));
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
}

std::string RealInstalledRuntimeTest::mount_unit() const {
    return require_output(
        {"systemd-escape", "-p", "--suffix=mount", target_mount_.string()},
        "derive target mount unit"
    );
}

void RealInstalledRuntimeTest::configure_and_install() const {
    const fs::path rendered_config = rendered_root_ / "config";
    const fs::path rendered_systemd = rendered_root_ / "systemd";
    const fs::path rendered_udev = rendered_root_ / "udev";
    const fs::path installed_keyfile = "/etc/btrfs-backup/keys/default.key";
    const fs::path saved_profile = rendered_config / "profiles/default/profile.json";
    const std::string luks_uuid = require_output(
        {"cryptsetup", "luksUUID", target_device_.string()},
        "read target LUKS UUID"
    );
    const std::string btrfs_uuid = require_output(
        {"findmnt", "-n", "-o", "UUID", "-M", target_mount_.string()},
        "read target Btrfs UUID"
    );

    require_success({"install", "-d", "-m0700", "/etc/btrfs-backup/keys"}, "create key directory");
    require_success(
        {"install", "-m0600", passphrase_file_.string(), installed_keyfile.string()},
        "install integration key"
    );
    require_success(
        {"install", "-d", "-m0750", rendered_config.string(), rendered_systemd.string(), rendered_udev.string()},
        "create rendered configuration directories"
    );
    require_success(
        {backupctl_.string(), "profile", "create", "--output", (rendered_config / "profile.json").string(), "--profile", std::string(profile_id), "--name", "Default backup", "--device", target_device_.string(), "--luks-uuid", luks_uuid, "--btrfs-uuid", btrfs_uuid, "--mapper-name", mapper_name_, "--keyfile", installed_keyfile.string(), "--remote-retention", "2", "--local-retention", "2", "--daily-limit", "false", "--incremental-required", "true", "--keep-failed-local-snapshot", "false", "--auto-eject", "false", "--minimum-target-free-bytes", "0", "--minimum-local-free-bytes", "0", "--source", "home", "home", (source_mount_ / "home").string(), (source_mount_ / ".snapshots/home").string(), "home", "2", "2"},
        "create integration profile"
    );
    require_success(
        {backupctl_.string(), "profile", "--etc-root", rendered_config.string(), "--udev-root", rendered_udev.string(), "--systemd-root", rendered_systemd.string(), "--public-root", (rendered_root_ / "public/profiles").string(), "save", "--file", (rendered_config / "profile.json").string()},
        "save integration profile"
    );
    require_success(
        {"install", "-m0600", saved_profile.string(), (rendered_config / "profile.json").string()},
        "select saved integration profile"
    );
    require_success(
        {backupctl_.string(), "installation", "render", "--file", (rendered_config / "profile.json").string(), "--output-dir", rendered_root_.string(), "--backup-command", "/usr/bin/btrfs-backupctl runner execute", "--eject-script", "/usr/bin/btrfs-backupctl target eject"},
        "render integration installation"
    );
    require_success(
        {backupctl_.string(), "installation", "validate", "--rendered-root", rendered_root_.string()},
        "validate rendered installation"
    );

    require_success(
        {"install", "-d", "-m0700", "/etc/btrfs-backup", "/etc/btrfs-backup/profiles/default"},
        "create active configuration directories"
    );
    require_success({"install", "-m0600", saved_profile.string(), profile_path.string()}, "install profile");
    const std::vector<std::string> service_files{
        "btrfs-backup.service",
        "btrfs-backup@.service",
        "btrfs-backup-eject@.service",
        "btrfs-backup-validate@.service",
        "btrfs-backup-target@.service",
        mount_unit(),
    };
    for (const auto& name : service_files)
        require_success(
            {"install", "-Dm0644", (rendered_systemd / name).string(), (fs::path("/etc/systemd/system") / name).string()},
            "install systemd unit " + name
        );
    require_success(
        {"install", "-Dm0644", (rendered_systemd / "btrfs-backup@default.service.d/target-mount.conf").string(), "/etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf"},
        "install backup mount dependency"
    );
    require_success(
        {"install", "-Dm0644", (rendered_udev / "99-btrfs-backup-default.rules").string(), "/etc/udev/rules.d/99-btrfs-backup-default.rules"},
        "install target udev rule"
    );
    require_success({"systemctl", "daemon-reload"}, "reload installed units");
    require_success(
        {backupctl_.string(), "installation", "validate", "--active", "--profile", std::string(profile_id)},
        "validate active installation"
    );
    if (!fs::is_regular_file(profile_path))
        throw std::runtime_error("configuration did not create default profile JSON");
}

void RealInstalledRuntimeTest::activate_managed_target() const {
    require_success({"umount", target_mount_.string()}, "unmount target before managed activation");
    require_success({"cryptsetup", "close", mapper_name_}, "close mapper before managed activation");
    if (fs::exists(mapper_path_))
        throw std::runtime_error("test mapper remained active before managed activation");
    require_success({"systemctl", "start", "systemd-udevd.service"}, "start udev before managed activation");
    require_success({"systemctl", "is-active", "--quiet", "systemd-udevd.service"}, "verify active udev");
    require_success({"systemctl", "start", mount_unit()}, "activate and mount managed target");
    require_success({"findmnt", "-n", "-M", target_mount_.string()}, "verify managed target mount");
    if (!fs::is_block_file(mapper_path_))
        throw std::runtime_error("managed target service did not activate the LUKS mapper");
    if (!fs::is_regular_file("/run/btrfs-backup/target-activation/default.json"))
        throw std::runtime_error("managed target activation did not record mapper ownership");
}

void RealInstalledRuntimeTest::validate_runtime() const {
    require_success(
        {"env", "INVOCATION_ID=real-docker-test", runtime_.string(), "--validate", "--no-eject"},
        "validate installed runtime preflight"
    );
    require_success(
        {backupctl_.string(), "target", "mount", "--profile", std::string(profile_id)},
        "validate installed target mount command"
    );
}

} // namespace btrfsbackup::integration
