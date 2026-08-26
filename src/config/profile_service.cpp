// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/profile_service.hpp>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <config/application_config.hpp>
#include <config/errors.hpp>
#include <config/identifiers.hpp>
#include <config/json_io.hpp>
#include <config/profile_artifact_renderer.hpp>
#include <config/profile_installer.hpp>
#include <config/profile_loader.hpp>
#include <config/profile.hpp>
#include <config/render_directory.hpp>
#include <platform/linux/file_io.hpp>
#include <platform/linux/process.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

Profile validate_profile_file(const fs::path& file, const fs::path& target_mount_root) {
    return profile_from_json(load_json_file(file), target_mount_root);
}

void write_profile_file(const Profile& profile, const fs::path& output) {
    atomic_write(output, dump_json(profile_to_json(profile)), 0600);
}

void render_profile(const fs::path& file, const fs::path& output_dir, const fs::path& target_mount_root) {
    const Profile profile = validate_profile_file(file, target_mount_root);
    ProfileArtifactRenderer renderer(generate_configuration_generation);
    replace_render_directory(
        output_dir,
        [&](const fs::path& staging) {
            write_profile_artifacts(renderer.render_profile_artifacts(profile, profile_artifact_roots(staging)));
        },
        [&](const fs::path& staging) {
            const fs::path rendered = staging / "etc" / "btrfs-backup" / "profiles" / profile.id.value() / "profile.json";
            const Profile validated = validate_profile_file(rendered, target_mount_root);
            if (validated.id != profile.id) {
                throw ValidationError("rendered profile identity mismatch");
            }
        }
    );
}

Profile save_profile(const fs::path& file, const ProfileInstallationRoots& roots) {
    ApplicationConfig config = ApplicationConfig::load(roots.etc_root);
    Profile profile = validate_profile_file(file, config.paths().target_mount_root);
    ProfileArtifactRenderer renderer(generate_configuration_generation);
    const ProfileArtifactRoots artifact_roots{
        .etc_root = roots.etc_root,
        .udev_root = roots.udev_root,
        .systemd_root = roots.systemd_root,
        .public_root = roots.public_root,
    };
    if (fs::absolute(roots.etc_root).lexically_normal() == fs::path("/etc/btrfs-backup")
        && fs::absolute(roots.udev_root).lexically_normal() == fs::path("/etc/udev/rules.d")
        && fs::absolute(roots.systemd_root).lexically_normal() == fs::path("/etc/systemd/system")
        && fs::absolute(roots.public_root).lexically_normal()
            == fs::path("/var/lib/btrfs-backup/public/profiles")) {
        FunctionProfileActivation activation([] {
            run_capture({"systemctl", "daemon-reload"});
            run_capture({"udevadm", "control", "--reload-rules"});
        });
        ProfileInstaller installer(renderer, activation);
        installer.install_profile_transactionally(profile, artifact_roots);
    } else {
        NullProfileActivation activation;
        ProfileInstaller installer(renderer, activation);
        installer.install_profile_transactionally(profile, artifact_roots);
    }
    return profile;
}

Profile get_profile(const fs::path& etc_root, const std::string& profile_id) {
    return load_profile_by_id(etc_root, profile_id);
}

Profile export_profile(const fs::path& etc_root, const std::string& profile_id, const fs::path& output) {
    Profile profile = get_profile(etc_root, profile_id);
    write_profile_file(profile, output);
    return profile;
}

std::vector<std::string> list_profiles(const fs::path& profile_root) {
    std::set<std::string> profiles;
    std::error_code ec;
    if (fs::is_directory(profile_root, ec) && !ec) {
        for (const auto& entry : fs::directory_iterator(profile_root, ec)) {
            if (ec) break;
            fs::path profile_json = entry.path() / "profile.json";
            if (entry.is_directory(ec) && !ec && fs::is_regular_file(profile_json, ec) && !ec) {
                std::string id = entry.path().filename().string();
                validate_profile_id(id);
                profiles.insert(std::move(id));
            }
            ec.clear();
        }
    }
    if (profiles.empty()) {
        throw ValidationError("no profiles found");
    }
    return {profiles.begin(), profiles.end()};
}

} // namespace btrfsbackup
