#include <config/wizard/profile_wizard_install.hpp>

#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

#include <config/errors.hpp>
#include <platform/linux/file_io.hpp>
#include <config/installation_render.hpp>
#include <config/installation_validate.hpp>
#include <config/json_io.hpp>
#include <config/profile.hpp>
#include <platform/linux/process.hpp>
#include <platform/linux/trusted_directory.hpp>
#include <config/profile_store.hpp>
#include <config/wizard/profile_wizard_paths.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

void render_wizard_tree(const Profile& profile, const std::string& keyfile, const fs::path& output_dir) {
    wizard::assert_safe_output_dir(output_dir);
    std::error_code ec;
    fs::remove_all(output_dir, ec);
    fs::create_directories(output_dir / "config");
    fs::create_directories(output_dir / "systemd");
    fs::create_directories(output_dir / "udev");

    atomic_write(output_dir / "config" / "profile.json", dump_json(profile_to_json(profile)), 0600);
    save_tree(
        profile,
        output_dir / "config",
        output_dir / "udev",
        output_dir / "systemd",
        output_dir / "public" / "profiles"
    );

    render_installation_files(
        profile,
        output_dir,
        {
            "/usr/bin/btrfs-backupctl runner execute",
            "/usr/bin/btrfs-backupctl target eject",
            keyfile
        }
    );
    validate_rendered_installation(output_dir, fs::path(profile.target.mount_point).parent_path());
}

void apply_rendered_wizard_tree(const Profile& profile, const fs::path& output_dir) {
    if (geteuid() != 0) {
        throw ValidationError("apply must be run as root");
    }
    fs::create_directories("/etc/btrfs-backup");
    fs::create_directories("/etc/systemd/system");
    fs::create_directories("/etc/udev/rules.d");
    fs::create_directories("/var/lib/btrfs-backup/public/profiles");
    ensure_trusted_directory(profile.target.mount_point, 0755);

    auto activate = [&] {
        fs::copy_file(
            output_dir / "systemd" / "btrfs-backup.service",
            "/etc/systemd/system/btrfs-backup.service",
            fs::copy_options::overwrite_existing
        );
        fs::copy_file(
            output_dir / "systemd" / "btrfs-backup@.service",
            "/etc/systemd/system/btrfs-backup@.service",
            fs::copy_options::overwrite_existing
        );
        fs::copy_file(
            output_dir / "systemd" / "btrfs-backup-eject@.service",
            "/etc/systemd/system/btrfs-backup-eject@.service",
            fs::copy_options::overwrite_existing
        );
        std::error_code error;
        fs::remove("/etc/udev/rules.d/99-btrfs-backup.rules", error);
        run_command({"systemctl", "disable", "btrfs-backup.service"});
        run_capture({"systemctl", "daemon-reload"});
        run_capture({"udevadm", "control", "--reload-rules"});
    };
    save_tree(
        profile,
        "/etc/btrfs-backup",
        "/etc/udev/rules.d",
        "/etc/systemd/system",
        "/var/lib/btrfs-backup/public/profiles",
        activate
    );
    validate_active_installation(profile.id);
}

} // namespace btrfsbackup
