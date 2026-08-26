// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/wizard/profile_wizard_install.hpp>

#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <core/errors.hpp>
#include <platform/linux/file_io.hpp>
#include <config/installation_render.hpp>
#include <config/installation_validate.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile.hpp>
#include <config/profile_artifact_renderer.hpp>
#include <config/profile_installer.hpp>
#include <config/render_directory.hpp>
#include <platform/linux/linux_system_configuration_activator.hpp>
#include <platform/linux/process.hpp>
#include <platform/linux/trusted_directory.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

class WizardConfigurationActivator final : public IConfigurationActivator {
  public:
    explicit WizardConfigurationActivator(fs::path rendered_root)
        : rendered_root_(std::move(rendered_root)) {
    }

    void activate() override {
        fs::copy_file(
            rendered_root_ / "systemd" / "btrfs-backup.service",
            "/etc/systemd/system/btrfs-backup.service",
            fs::copy_options::overwrite_existing
        );
        fs::copy_file(
            rendered_root_ / "systemd" / "btrfs-backup@.service",
            "/etc/systemd/system/btrfs-backup@.service",
            fs::copy_options::overwrite_existing
        );
        fs::copy_file(
            rendered_root_ / "systemd" / "btrfs-backup-eject@.service",
            "/etc/systemd/system/btrfs-backup-eject@.service",
            fs::copy_options::overwrite_existing
        );
        std::error_code error;
        fs::remove("/etc/udev/rules.d/99-btrfs-backup.rules", error);
        (void)run_command({"systemctl", "disable", "btrfs-backup.service"});
        system_configuration_.activate();
    }

  private:
    fs::path rendered_root_;
    LinuxSystemConfigurationActivator system_configuration_;
};

} // namespace

void render_wizard_tree(const Profile& profile, const std::string& keyfile, const fs::path& output_dir) {
    const fs::path target_mount_root = fs::path(profile.target.mount_point).parent_path();
    const Profile validated_profile = profile_from_json(profile_to_json(profile), target_mount_root);
    replace_render_directory(
        output_dir,
        [&](const fs::path& staging) {
            fs::create_directories(staging / "config");
            fs::create_directories(staging / "systemd");
            fs::create_directories(staging / "udev");

            atomic_write(staging / "config" / "profile.json", dump_json(profile_to_json(validated_profile)), 0600);
            ProfileArtifactRenderer renderer(generate_configuration_generation);
            NullConfigurationActivator activator;
            ProfileInstaller installer(renderer, activator);
            installer.install_profile_transactionally(
                validated_profile,
                {
                    .etc_root = staging / "config",
                    .udev_root = staging / "udev",
                    .systemd_root = staging / "systemd",
                    .public_root = staging / "public" / "profiles",
                }
            );

            render_installation_files(
                validated_profile,
                staging,
                {
                    "/usr/bin/btrfs-backupctl runner execute",
                    "/usr/bin/btrfs-backupctl target eject",
                    keyfile
                }
            );
        },
        [&](const fs::path& staging) {
            validate_rendered_installation(staging, target_mount_root);
        }
    );
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

    WizardConfigurationActivator activator(output_dir);
    ProfileArtifactRenderer renderer(generate_configuration_generation);
    ProfileInstaller installer(renderer, activator);
    installer.install_profile_transactionally(
        profile,
        {
            .etc_root = "/etc/btrfs-backup",
            .udev_root = "/etc/udev/rules.d",
            .systemd_root = "/etc/systemd/system",
            .public_root = "/var/lib/btrfs-backup/public/profiles",
        }
    );
    validate_active_installation(std::string(profile.id.value()));
}

} // namespace btrfsbackup
