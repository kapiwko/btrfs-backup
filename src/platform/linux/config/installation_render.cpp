// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/installation_render.hpp>

#include <filesystem>
#include <string>

#include <platform/linux/file_io.hpp>
#include <platform/linux/systemd_unit.hpp>
#include <config/profile_render.hpp>

namespace fs = std::filesystem;

namespace {

#ifndef BTRFSBACKUP_INSTALL_BINDIR
#error "BTRFSBACKUP_INSTALL_BINDIR must be defined by the build system"
#endif

constexpr const char* service_hardening =
    "NoNewPrivileges=yes\n"
    "PrivateTmp=yes\n"
    "ProtectSystem=full\n"
    "ProtectKernelTunables=yes\n"
    "ProtectKernelModules=yes\n"
    "ProtectControlGroups=yes\n"
    "ProtectHostname=yes\n"
    "ProtectClock=yes\n"
    "ProtectProc=invisible\n"
    "LockPersonality=yes\n"
    "RestrictRealtime=yes\n"
    "MemoryDenyWriteExecute=yes\n"
    "SystemCallArchitectures=native\n"
    "RestrictAddressFamilies=AF_UNIX AF_NETLINK\n";

constexpr const char* eject_service_hardening =
    "NoNewPrivileges=yes\n"
    "ProtectHostname=yes\n"
    "LockPersonality=yes\n"
    "RestrictRealtime=yes\n"
    "MemoryDenyWriteExecute=yes\n"
    "SystemCallArchitectures=native\n"
    "RestrictAddressFamilies=AF_UNIX AF_NETLINK\n";

std::string render_backup_service(const btrfsbackup::config::Profile& profile, const std::string& backup_command) {
    return "[Unit]\n"
           "Description=Verified Btrfs backup to an encrypted removable target\n"
           "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
           "ConditionPathExists=/etc/btrfs-backup\n" +
        btrfsbackup::config::render_mount_requirement(profile) +
        "After=local-fs.target systemd-udevd.service\n"
        "StartLimitIntervalSec=5min\n"
        "StartLimitBurst=3\n"
        "OnSuccess=btrfs-backup-eject@" +
        std::string(profile.id.value()) + ".service\n"
                                          "OnFailure=btrfs-backup-eject@" +
        std::string(profile.id.value()) + ".service\n"
                                          "\n"
                                          "[Service]\n"
                                          "Type=oneshot\n"
                                          "ExecStart=" +
        backup_command + " --profile " + std::string(profile.id.value()) + "\n"
                                                                           "User=root\n"
                                                                           "Group=root\n"
                                                                           "UMask=0077\n"
                                                                           "RuntimeDirectory=btrfs-backup\n"
                                                                           "RuntimeDirectoryMode=0755\n"
                                                                           "StateDirectory=btrfs-backup\n"
                                                                           "StateDirectoryMode=0755\n"
                                                                           "Environment=PATH=/usr/bin\n" +
        service_hardening +
        "Nice=10\n"
        "IOSchedulingClass=best-effort\n"
        "IOSchedulingPriority=7\n"
        "TimeoutStartSec=infinity\n"
        "TimeoutStopSec=90s\n"
        "KillSignal=SIGINT\n"
        "KillMode=mixed\n"
        "SendSIGKILL=yes\n"
        "SyslogIdentifier=btrfs-backup\n";
}

std::string render_profile_service(const std::string& backup_command) {
    return "[Unit]\n"
           "Description=Verified Btrfs backup profile %i to an encrypted removable target\n"
           "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
           "ConditionPathExists=/etc/btrfs-backup\n"
           "After=local-fs.target systemd-udevd.service\n"
           "StartLimitIntervalSec=5min\n"
           "StartLimitBurst=3\n"
           "OnSuccess=btrfs-backup-eject@%i.service\n"
           "OnFailure=btrfs-backup-eject@%i.service\n"
           "\n"
           "[Service]\n"
           "Type=oneshot\n"
           "ExecStart=" +
        backup_command + " --profile %i\n"
                         "User=root\n"
                         "Group=root\n"
                         "UMask=0077\n"
                         "RuntimeDirectory=btrfs-backup\n"
                         "RuntimeDirectoryMode=0755\n"
                         "StateDirectory=btrfs-backup\n"
                         "StateDirectoryMode=0755\n"
                         "Environment=PATH=/usr/bin\n" +
        service_hardening +
        "Nice=10\n"
        "IOSchedulingClass=best-effort\n"
        "IOSchedulingPriority=7\n"
        "TimeoutStartSec=infinity\n"
        "TimeoutStopSec=90s\n"
        "KillSignal=SIGINT\n"
        "KillMode=mixed\n"
        "SendSIGKILL=yes\n"
        "SyslogIdentifier=btrfs-backup\n";
}

std::string render_eject_service(const std::string& eject_script) {
    return "[Unit]\n"
           "Description=Safely eject Btrfs backup target for profile %i\n"
           "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
           "ConditionPathExists=/etc/btrfs-backup\n"
           "After=btrfs-backup.service btrfs-backup@%i.service\n"
           "\n"
           "[Service]\n"
           "Type=oneshot\n"
           "ExecStart=" +
        eject_script + " --from-service --profile %i\n"
                       "User=root\n"
                       "Group=root\n"
                       "UMask=0077\n"
                       "RuntimeDirectory=btrfs-backup\n"
                       "RuntimeDirectoryMode=0755\n"
                       "StateDirectory=btrfs-backup\n"
                       "StateDirectoryMode=0755\n"
                       "Environment=PATH=/usr/bin\n" +
        eject_service_hardening +
        "TimeoutStartSec=90s\n"
        "SyslogIdentifier=btrfs-backup-eject\n";
}

std::string render_validate_service(const std::string& backup_command) {
    return "[Unit]\n"
           "Description=Validate Btrfs backup target for authorized operation %i\n"
           "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
           "ConditionPathExists=/etc/btrfs-backup\n"
           "After=local-fs.target systemd-udevd.service\n"
           "\n"
           "[Service]\n"
           "Type=oneshot\n"
           "EnvironmentFile=/run/btrfs-backup-manager/%i.env\n"
           "ExecStart=" +
        backup_command + " --profile ${BTRFS_BACKUP_PROFILE_ID} --validate\n"
                         "User=root\n"
                         "Group=root\n"
                         "UMask=0077\n"
                         "RuntimeDirectory=btrfs-backup\n"
                         "RuntimeDirectoryMode=0755\n"
                         "StateDirectory=btrfs-backup\n"
                         "StateDirectoryMode=0755\n"
                         "Environment=PATH=/usr/bin\n" +
        service_hardening +
        "Nice=10\n"
        "IOSchedulingClass=best-effort\n"
        "IOSchedulingPriority=7\n"
        "TimeoutStartSec=10min\n"
        "TimeoutStopSec=90s\n"
        "KillSignal=SIGINT\n"
        "KillMode=mixed\n"
        "SendSIGKILL=yes\n"
        "SyslogIdentifier=btrfs-backup-validate\n";
}

std::string render_target_service(const std::string& target_command) {
    return "[Unit]\n"
           "Description=Activate encrypted backup target for profile %i\n"
           "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
           "DefaultDependencies=no\n"
           "After=systemd-udevd.service\n"
           "Before=umount.target\n"
           "Conflicts=umount.target\n"
           "\n"
           "[Service]\n"
           "Type=oneshot\n"
           "RemainAfterExit=yes\n"
           "ExecStart=" +
        target_command +
        " activate --from-service --profile %i\n"
        "ExecStop=" +
        target_command +
        " deactivate --from-service --profile %i\n"
        "User=root\n"
        "Group=root\n"
        "UMask=0077\n"
        "Environment=PATH=/usr/bin\n"
        "NoNewPrivileges=yes\n"
        "ProtectSystem=full\n"
        "ProtectKernelTunables=yes\n"
        "ProtectKernelModules=yes\n"
        "ProtectControlGroups=yes\n"
        "ProtectHostname=yes\n"
        "ProtectClock=yes\n"
        "ProtectProc=invisible\n"
        "LockPersonality=yes\n"
        "RestrictRealtime=yes\n"
        "MemoryDenyWriteExecute=yes\n"
        "SystemCallArchitectures=native\n"
        "RestrictAddressFamilies=AF_UNIX AF_NETLINK\n"
        "TimeoutStartSec=90s\n"
        "TimeoutStopSec=90s\n"
        "SyslogIdentifier=btrfs-backup-target\n";
}

} // namespace

namespace btrfsbackup::platform::linux {

std::string default_backup_command() {
    return std::string(BTRFSBACKUP_INSTALL_BINDIR) + "/btrfs-backupctl runner execute";
}

std::string default_eject_script() {
    return std::string(BTRFSBACKUP_INSTALL_BINDIR) + "/btrfs-backupctl target eject";
}

std::string default_target_command() {
    return std::string(BTRFSBACKUP_INSTALL_BINDIR) + "/btrfs-backupctl target";
}

void render_installation_files(
    const btrfsbackup::config::Profile& profile,
    const fs::path& output_dir,
    const InstallationRenderOptions& options
) {
    fs::create_directories(output_dir / "systemd");
    atomic_write(output_dir / "systemd" / "btrfs-backup.service", render_backup_service(profile, options.backup_command), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup@.service", render_profile_service(options.backup_command), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup-eject@.service", render_eject_service(options.eject_script), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup-validate@.service", render_validate_service(options.backup_command), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup-target@.service", render_target_service(options.target_command), 0644);
    atomic_write(
        output_dir / "systemd" / btrfsbackup::platform::linux::systemd_mount_unit_name(profile.target.mount_point),
        btrfsbackup::config::render_target_mount_unit(profile),
        0644
    );
    atomic_write(
        output_dir / "systemd" / ("btrfs-backup@" + std::string(profile.id.value()) + ".service.d") / "target-mount.conf",
        btrfsbackup::config::render_mount_dependency(profile),
        0644
    );
}

} // namespace btrfsbackup::platform::linux
