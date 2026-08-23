#include <btrfsbackup/profile_store.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_render.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

std::string iso_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &tm);
    return buffer;
}

} // namespace

void render_tree(const Json& profile, const fs::path& output_dir) {
    std::string profile_id = profile.at("profileId").get<std::string>();
    fs::path root = output_dir / "etc" / "btrfs-backup";
    atomic_write(root / "profiles.d" / (profile_id + ".env"), render_profile_env(profile), 0600);
    int index = 1;
    for (const auto& source : profile.at("sources")) {
        std::ostringstream name;
        name << std::setw(3) << std::setfill('0') << index * 10 << "-" << source.at("id").get<std::string>() << ".conf";
        atomic_write(root / "profiles" / profile_id / "sources.d" / name.str(), render_source(source), 0600);
        ++index;
    }
    atomic_write(output_dir / "etc" / "udev" / "rules.d" / ("99-btrfs-backup-" + profile_id + ".rules"), render_udev(profile), 0644);
    Json public_profile = profile;
    public_profile["generatedAt"] = iso_now();
    atomic_write(output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / (profile_id + ".json"), dump_json(public_profile), 0644);
}

void save_tree(const Json& profile, const fs::path& etc_root, const fs::path& udev_root, const fs::path& public_root) {
    std::string profile_id = profile.at("profileId").get<std::string>();
    fs::path source_root = map_etc_path(profile.at("paths").at("sourcesDir").get<std::string>(), etc_root);
    atomic_write(etc_root / "profiles.d" / (profile_id + ".env"), render_profile_env(profile), 0600);
    if (fs::exists(source_root)) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &tm);
        fs::rename(source_root, source_root.parent_path() / (source_root.filename().string() + ".backup-" + stamp));
    }
    int index = 1;
    for (const auto& source : profile.at("sources")) {
        std::ostringstream name;
        name << std::setw(3) << std::setfill('0') << index * 10 << "-" << source.at("id").get<std::string>() << ".conf";
        atomic_write(source_root / name.str(), render_source(source), 0600);
        ++index;
    }
    atomic_write(udev_root / ("99-btrfs-backup-" + profile_id + ".rules"), render_udev(profile), 0644);
    Json public_profile = profile;
    public_profile["generatedAt"] = iso_now();
    atomic_write(public_root / (profile_id + ".json"), dump_json(public_profile), 0644);
}

} // namespace btrfsbackup
