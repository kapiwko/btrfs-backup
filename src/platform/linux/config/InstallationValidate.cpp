// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/InstallationValidate.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <core/Errors.hpp>
#include <platform/linux/config/ApplicationConfig.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <config/model/JsonIo.hpp>
#include <platform/linux/process/Process.hpp>
#include <platform/linux/systemd/SystemdUnit.hpp>
#include <config/model/Profile.hpp>
#include <config/model/ProfileDocument.hpp>
#include <config/ProfileRender.hpp>

namespace fs = std::filesystem;

namespace {

bool contains_unresolved_placeholder(const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            throw btrfsbackup::ValidationError("cannot scan rendered tree: " + root.string());
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        std::ifstream stream(it->path());
        std::string line;
        while (std::getline(stream, line)) {
            if (line.contains("{{") && line.contains("}}")) {
                return true;
            }
        }
    }
    return false;
}

bool allowed_systemd_verify_failure(const std::string& output, bool allow_missing_executables) {
    bool saw_output = false;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        saw_output = true;
        bool missing_executable = allow_missing_executables && line.contains(".service: Command ") && line.ends_with(" is not executable: No such file or directory");
        if (line != "Failed to turn off SO_PASSRIGHTS on user lookup socket, ignoring: Operation not permitted" && line != "Failed to enable SO_PASSCRED on handoff timestamp socket: Operation not permitted" && !missing_executable) {
            return false;
        }
    }
    return saw_output;
}

void run_checked(
    const std::vector<std::string>& argv,
    bool allow_systemd_warnings = false,
    bool allow_missing_executables = false
) {
    btrfsbackup::backup::CommandResult result = btrfsbackup::platform::linux::process::run_command(argv);
    if (result.exit_code == 0) {
        if (!result.output.empty()) {
            std::cerr << result.output;
        }
        return;
    }
    if (allow_systemd_warnings && allowed_systemd_verify_failure(result.output, allow_missing_executables)) {
        std::cerr << result.output;
        return;
    }
    if (!result.output.empty()) {
        std::cerr << result.output;
    }
    throw btrfsbackup::ValidationError("command failed: " + argv.front());
}

void require_file(const fs::path& path, const std::string& label) {
    if (!fs::is_regular_file(path)) {
        throw btrfsbackup::ValidationError(label + ": " + path.string());
    }
}

void require_exact_text(const fs::path& path, const std::string& expected, const std::string& label) {
    require_file(path, label);
    std::ifstream stream(path);
    std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (content != expected) {
        throw btrfsbackup::ValidationError(label + " has unexpected content: " + path.string());
    }
}

} // namespace

namespace btrfsbackup::platform::linux {

void validate_rendered_installation(const fs::path& root, const fs::path& target_mount_root) {
    fs::path profile_json = root / "config" / "profile.json";
    fs::path service_file = root / "systemd" / "btrfs-backup.service";
    fs::path profile_service_file = root / "systemd" / "btrfs-backup@.service";
    fs::path eject_service_file = root / "systemd" / "btrfs-backup-eject@.service";
    fs::path validate_service_file = root / "systemd" / "btrfs-backup-validate@.service";
    fs::path target_service_file = root / "systemd" / "btrfs-backup-target@.service";

    require_file(profile_json, "missing rendered canonical profile JSON");
    require_file(service_file, "missing rendered systemd unit");
    require_file(profile_service_file, "missing rendered systemd template unit");
    require_file(eject_service_file, "missing rendered eject systemd template unit");
    require_file(validate_service_file, "missing rendered validation systemd template unit");
    require_file(target_service_file, "missing rendered target systemd template unit");
    if (contains_unresolved_placeholder(root)) {
        throw ValidationError("unresolved placeholders remain in rendered files");
    }

    btrfsbackup::config::Profile profile = validate_profile_file(profile_json, target_mount_root);
    fs::path mount_dependency = root / "systemd" / ("btrfs-backup@" + std::string(profile.id.value()) + ".service.d") / "target-mount.conf";
    require_exact_text(
        mount_dependency,
        btrfsbackup::config::render_mount_dependency(profile),
        "missing rendered target mount dependency"
    );
    fs::path mount_unit = root / "systemd" / systemd::systemd_mount_unit_name(profile.target.mount_point);
    require_exact_text(
        mount_unit,
        btrfsbackup::config::render_target_mount_unit(profile),
        "missing rendered native target mount"
    );
    fs::path udev_file = root / "udev" / ("99-btrfs-backup-" + std::string(profile.id.value()) + ".rules");
    require_file(udev_file, "missing rendered profile udev rule");
    run_checked(
        {
            "systemd-analyze",
            "verify",
            service_file.string(),
            profile_service_file.string(),
            eject_service_file.string(),
            validate_service_file.string(),
            target_service_file.string(),
            mount_unit.string(),
        },
        true,
        true
    );
    run_checked({"udevadm", "verify", udev_file.string()});
    std::cerr << "Rendered configuration passed syntax, systemd, and udev validation: " << root << '\n';
}

void validate_active_installation(const std::string& profile_id) {
    if (geteuid() != 0) {
        throw ValidationError("active installation validation must be run as root");
    }
    fs::path profile_json = fs::path("/etc/btrfs-backup/profiles") / profile_id / "profile.json";
    fs::path service_file = "/etc/systemd/system/btrfs-backup.service";
    fs::path profile_service_file = "/etc/systemd/system/btrfs-backup@.service";
    fs::path eject_service_file = "/etc/systemd/system/btrfs-backup-eject@.service";
    fs::path validate_service_file = "/etc/systemd/system/btrfs-backup-validate@.service";
    const std::optional<fs::path> target_service_file =
        systemd::locate_systemd_unit_file("btrfs-backup-target@.service");
    if (!target_service_file.has_value()) {
        throw ValidationError(
            "missing target systemd template unit in the systemd unit load path: "
            "btrfs-backup-target@.service"
        );
    }

    require_file(profile_json, "missing profile JSON");
    if (!fs::is_regular_file(service_file)) {
        throw ValidationError("missing " + service_file.string());
    }
    require_file(eject_service_file, "missing eject systemd template unit");
    require_file(validate_service_file, "missing validation systemd template unit");

    btrfsbackup::config::ApplicationConfig config = load_application_config();
    btrfsbackup::config::Profile profile = validate_profile_file(profile_json, config.paths().target_mount_root);
    fs::path mount_dependency = fs::path("/etc/systemd/system") / ("btrfs-backup@" + std::string(profile.id.value()) + ".service.d") / "target-mount.conf";
    require_exact_text(mount_dependency, btrfsbackup::config::render_mount_dependency(profile), "missing target mount dependency");
    fs::path native_mount_unit = fs::path("/etc/systemd/system") / systemd::systemd_mount_unit_name(profile.target.mount_point);
    require_exact_text(
        native_mount_unit,
        btrfsbackup::config::render_target_mount_unit(profile),
        "missing native target mount"
    );
    fs::path udev_file = fs::path("/etc/udev/rules.d") / ("99-btrfs-backup-" + std::string(profile.id.value()) + ".rules");
    if (!fs::is_regular_file(udev_file)) {
        throw ValidationError("missing " + udev_file.string());
    }
    std::vector<std::string> verify_units = {"systemd-analyze", "verify", service_file.string()};
    if (fs::is_regular_file(profile_service_file)) {
        verify_units.push_back(profile_service_file.string());
    }
    verify_units.push_back(eject_service_file.string());
    verify_units.push_back(validate_service_file.string());
    verify_units.push_back(target_service_file->string());
    verify_units.push_back(native_mount_unit.string());
    run_checked(verify_units, true);
    run_checked({"udevadm", "verify", udev_file.string()});

    std::string mount_unit = process::run_capture({"systemd-escape", "-p", "--suffix=mount", profile.target.mount_point.value().string()});
    fs::path old_dropin = fs::path("/etc/systemd/system") / (mount_unit + ".d") / "backup.conf";
    if (fs::is_regular_file(old_dropin)) {
        std::ifstream stream(old_dropin);
        std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if ((content.contains("Wants=") || content.contains("After=")) &&
            content.contains("btrfs-backup.service")) {
            throw ValidationError("obsolete cyclic mount drop-in still exists: " + old_dropin.string());
        }
    }

    std::cerr << "Active static configuration is valid. Run 'sudo btrfs-backup --validate' with the target connected for runtime validation.\n";
}

} // namespace btrfsbackup::platform::linux
