#include <btrfsbackup/profile_store.hpp>

#include <filesystem>
#include <string>
#include <utility>

#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/application_config.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_render.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

Json public_profile_json(const Profile& profile) {
    Json sources = Json::array();
    for (const auto& source : profile.sources) {
        sources.push_back({{"id", source.id}, {"name", source.name}});
    }
    return {
        {"schemaVersion", 1},
        {"profileId", profile.id},
        {"name", profile.name},
        {"target", {{"name", profile.target.mapper_name}}},
        {"sources", std::move(sources)},
    };
}

} // namespace

void render_tree(const Profile& profile, const fs::path& output_dir) {
    fs::path root = output_dir / "etc" / "btrfs-backup";
    atomic_write(root / "profiles" / profile.id / "profile.json", dump_json(profile_to_json(profile)), 0600);
    atomic_write(output_dir / "etc" / "udev" / "rules.d" / ("99-btrfs-backup-" + profile.id + ".rules"), render_udev(profile), 0644);
    atomic_write(
        output_dir / "etc" / "systemd" / "system" / ("btrfs-backup@" + profile.id + ".service.d") / "target-mount.conf",
        render_mount_dependency(profile),
        0644
    );
    atomic_write(
        output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / (profile.id + ".json"),
        dump_json(public_profile_json(profile)),
        0644
    );
}

void save_tree(
    const Profile& profile,
    const fs::path& etc_root,
    const fs::path& udev_root,
    const fs::path& systemd_root,
    const fs::path& public_root
) {
    ApplicationConfig application_config = ApplicationConfig::load(etc_root);
    fs::path source_root = profile_sources_dir(application_config.paths(), profile.id);
    atomic_write(etc_root / "profiles" / profile.id / "profile.json", dump_json(profile_to_json(profile)), 0600);
    if (fs::exists(source_root)) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &tm);
        fs::rename(source_root, source_root.parent_path() / (source_root.filename().string() + ".backup-" + stamp));
    }
    atomic_write(udev_root / ("99-btrfs-backup-" + profile.id + ".rules"), render_udev(profile), 0644);
    atomic_write(
        systemd_root / ("btrfs-backup@" + profile.id + ".service.d") / "target-mount.conf",
        render_mount_dependency(profile),
        0644
    );
    atomic_write(public_root / (profile.id + ".json"), dump_json(public_profile_json(profile)), 0644);
}

} // namespace btrfsbackup
