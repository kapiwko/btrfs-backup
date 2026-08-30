// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileWizardInstall.hpp>

#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <core/Errors.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/config/InstallationRender.hpp>
#include <platform/linux/config/InstallationValidate.hpp>
#include <config/json/JsonIo.hpp>
#include <config/domain/Profile.hpp>
#include <config/json/ProfileDocument.hpp>
#include <config/ProfileArtifactRenderer.hpp>
#include <platform/linux/config/ProfileArtifactIo.hpp>
#include <platform/linux/config/ProfileInstaller.hpp>
#include <platform/linux/config/ProfileRuntimePolicy.hpp>
#include <platform/linux/config/RenderDirectory.hpp>
#include <platform/linux/systemd/LinuxSystemConfigurationActivator.hpp>
#include <platform/linux/process/Process.hpp>
#include <platform/linux/filesystem/TrustedDirectory.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::config {

namespace {

class WizardConfigurationActivator final : public btrfsbackup::config::IConfigurationActivator {
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
        fs::copy_file(
            rendered_root_ / "systemd" / "btrfs-backup-validate@.service",
            "/etc/systemd/system/btrfs-backup-validate@.service",
            fs::copy_options::overwrite_existing
        );
        fs::copy_file(
            rendered_root_ / "systemd" / "btrfs-backup-target@.service",
            "/etc/systemd/system/btrfs-backup-target@.service",
            fs::copy_options::overwrite_existing
        );
        std::error_code error;
        fs::remove("/etc/udev/rules.d/99-btrfs-backup.rules", error);
        (void)process::run_command({"systemctl", "disable", "btrfs-backup.service"});
        system_configuration_.activate();
    }

  private:
    fs::path rendered_root_;
    systemd::LinuxSystemConfigurationActivator system_configuration_;
};

} // namespace

void render_wizard_tree(const btrfsbackup::config::Profile& profile, const fs::path& output_dir) {
    const fs::path target_mount_root = fs::path(profile.target.mount_point).parent_path();
    const btrfsbackup::config::Profile validated_profile = btrfsbackup::config::json::profile_from_json(btrfsbackup::config::json::profile_to_json(profile), target_mount_root);
    validate_profile_runtime_policy(validated_profile);
    replace_render_directory(
        output_dir,
        [&](const fs::path& staging) {
            fs::create_directories(staging / "config");
            fs::create_directories(staging / "systemd");
            fs::create_directories(staging / "udev");

            filesystem::atomic_write(staging / "config" / "profile.json", btrfsbackup::config::json::dump_json(btrfsbackup::config::json::profile_to_json(validated_profile)), 0600);
            btrfsbackup::config::ProfileArtifactRenderer renderer(generate_configuration_generation);
            btrfsbackup::config::NullConfigurationActivator activator;
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

            render_installation_files(validated_profile, staging, {});
        },
        [&](const fs::path& staging) {
            validate_rendered_installation(staging, target_mount_root);
        }
    );
}

void apply_rendered_wizard_tree(const btrfsbackup::config::Profile& profile, const fs::path& output_dir) {
    if (geteuid() != 0) {
        throw ValidationError("apply must be run as root");
    }
    fs::create_directories("/etc/btrfs-backup");
    fs::create_directories("/etc/systemd/system");
    fs::create_directories("/etc/udev/rules.d");
    fs::create_directories("/var/lib/btrfs-backup/public/profiles");
    filesystem::ensure_trusted_directory(profile.target.mount_point, 0755);

    WizardConfigurationActivator activator(output_dir);
    btrfsbackup::config::ProfileArtifactRenderer renderer(generate_configuration_generation);
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

} // namespace btrfsbackup::platform::linux::config
