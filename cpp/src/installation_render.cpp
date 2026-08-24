#include <btrfsbackup/installation_render.hpp>

#include <filesystem>
#include <string>

#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/process.hpp>
#include <btrfsbackup/profile_render.hpp>

namespace fs = std::filesystem;

namespace {

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

std::string fstab_escape(const std::string& value) {
    std::string escaped;
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\134"; break;
            case '\t': escaped += "\\011"; break;
            case ' ': escaped += "\\040"; break;
            case '#': escaped += "\\043"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

std::string render_fstab_fragment(const btrfsbackup::Profile& profile) {
    std::string cryptsetup_unit = btrfsbackup::run_capture({
        "systemd-escape",
        "--template=systemd-cryptsetup@.service",
        profile.target.mapper_name
    });
    return
        "# Merge this single line into /etc/fstab.\n"
        "# noauto prevents mounting at boot. The profile service starts this mount unit only after the matching LUKS device appears.\n"
        "\n"
        "/dev/mapper/" + fstab_escape(profile.target.mapper_name) + "  " +
        fstab_escape(profile.target.mount_point) +
        "  btrfs  noauto,nofail,noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd,x-systemd.requires=" +
        cryptsetup_unit +
        ",x-systemd.device-timeout=30s,x-systemd.mount-timeout=60s  0  0\n";
}

std::string render_crypttab_fragment(const btrfsbackup::Profile& profile, const std::string& keyfile) {
    return
        "# Merge this single line into /etc/crypttab.\n"
        "# Format: <name> <device> <password> <options>\n"
        "\n" +
        profile.target.mapper_name + "  UUID=" + profile.target.luks_uuid + "  " +
        fstab_escape(keyfile) +
        "  luks,noauto,nofail,x-systemd.device-timeout=30s\n";
}

std::string render_backup_service(const btrfsbackup::Profile& profile, const std::string& backup_command) {
    return
        "[Unit]\n"
        "Description=Verified Btrfs backup to an encrypted removable target\n"
        "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
        "ConditionPathExists=/etc/btrfs-backup\n"
        + render_mount_requirement(profile) +
        "After=local-fs.target systemd-udevd.service\n"
        "StartLimitIntervalSec=5min\n"
        "StartLimitBurst=3\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=" + backup_command + " --profile " + profile.id + "\n"
        "ExecStopPost=/usr/bin/systemctl --no-block start btrfs-backup-eject@" + profile.id + ".service\n"
        "User=root\n"
        "Group=root\n"
        "UMask=0077\n"
        "RuntimeDirectory=btrfs-backup\n"
        "RuntimeDirectoryMode=0755\n"
        "StateDirectory=btrfs-backup\n"
        "StateDirectoryMode=0755\n"
        "Environment=PATH=/usr/bin\n"
        + service_hardening +
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
    return
        "[Unit]\n"
        "Description=Verified Btrfs backup profile %i to an encrypted removable target\n"
        "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
        "ConditionPathExists=/etc/btrfs-backup\n"
        "After=local-fs.target systemd-udevd.service\n"
        "StartLimitIntervalSec=5min\n"
        "StartLimitBurst=3\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=" + backup_command + " --profile %i\n"
        "ExecStopPost=/usr/bin/systemctl --no-block start btrfs-backup-eject@%i.service\n"
        "User=root\n"
        "Group=root\n"
        "UMask=0077\n"
        "RuntimeDirectory=btrfs-backup\n"
        "RuntimeDirectoryMode=0755\n"
        "StateDirectory=btrfs-backup\n"
        "StateDirectoryMode=0755\n"
        "Environment=PATH=/usr/bin\n"
        + service_hardening +
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
    return
        "[Unit]\n"
        "Description=Safely eject Btrfs backup target for profile %i\n"
        "Documentation=file:/usr/share/doc/btrfs-backup/README.md\n"
        "ConditionPathExists=/etc/btrfs-backup\n"
        "After=btrfs-backup.service btrfs-backup@%i.service\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=" + eject_script + " --from-service --profile %i\n"
        "User=root\n"
        "Group=root\n"
        "UMask=0077\n"
        "RuntimeDirectory=btrfs-backup\n"
        "RuntimeDirectoryMode=0755\n"
        "StateDirectory=btrfs-backup\n"
        "StateDirectoryMode=0755\n"
        "Environment=PATH=/usr/bin\n"
        + eject_service_hardening +
        "TimeoutStartSec=90s\n"
        "SyslogIdentifier=btrfs-backup-eject\n";
}

} // namespace

namespace btrfsbackup {

void render_installation_files(
    const Profile& profile,
    const fs::path& output_dir,
    const InstallationRenderOptions& options
) {
    fs::create_directories(output_dir / "config");
    fs::create_directories(output_dir / "systemd");
    atomic_write(output_dir / "config" / "fstab.fragment", render_fstab_fragment(profile), 0644);
    atomic_write(output_dir / "config" / "crypttab.fragment", render_crypttab_fragment(profile, options.keyfile), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup.service", render_backup_service(profile, options.backup_command), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup@.service", render_profile_service(options.backup_command), 0644);
    atomic_write(output_dir / "systemd" / "btrfs-backup-eject@.service", render_eject_service(options.eject_script), 0644);
    atomic_write(
        output_dir / "systemd" / ("btrfs-backup@" + profile.id + ".service.d") / "target-mount.conf",
        render_mount_dependency(profile),
        0644
    );
}

} // namespace btrfsbackup
