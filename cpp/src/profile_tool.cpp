#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/profile_tool.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_render.hpp>

namespace fs = std::filesystem;
using json = btrfsbackup::Json;
using btrfsbackup::ValidationError;
using btrfsbackup::atomic_write;
using btrfsbackup::dump_json;
using btrfsbackup::env_bool;
using btrfsbackup::env_get;
using btrfsbackup::env_int;
using btrfsbackup::env_required;
using btrfsbackup::load_json_file;
using btrfsbackup::load_profile_by_id;
using btrfsbackup::map_etc_path;
using btrfsbackup::normalize_profile;
using btrfsbackup::render_profile_env;
using btrfsbackup::render_source;
using btrfsbackup::render_udev;

namespace {

constexpr int schema_version = 1;

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backup-profile: " << message << '\n';
    std::exit(code);
}

json profile_from_environment_sources(const fs::path& sources_table) {
    std::map<std::string, std::string> env;
    const std::vector<std::string> env_names{
        "PROFILE_ID", "PROFILE_NAME", "PROFILE_ROOT", "PROFILE_SOURCES_DIR",
        "BACKUP_DEVICE", "BACKUP_LUKS_UUID", "BACKUP_BTRFS_UUID",
        "BACKUP_PARTITION_UUID", "BACKUP_SERIAL", "BACKUP_MAPPER_NAME",
        "BACKUP_MOUNTPOINT", "REMOTE_ROOT", "INCOMING_ROOT", "STATE_DIR",
        "STATUS_ROOT", "HISTORY_ROOT", "RETENTION_COUNT", "LOCAL_RETENTION_COUNT",
        "DAILY_LIMIT", "INCREMENTAL_REQUIRED", "KEEP_FAILED_LOCAL_SNAPSHOT",
        "AUTO_EJECT", "MIN_TARGET_FREE_BYTES", "MIN_LOCAL_FREE_BYTES",
        "NOTIFY_ENABLE", "NOTIFY_USER", "NOTIFY_METHOD"
    };
    for (const auto& name : env_names) {
        if (const char* value = std::getenv(name.c_str())) {
            env[name] = value;
        }
    }

    std::string profile_id = env_required(env, "PROFILE_ID");
    std::string mount = env_required(env, "BACKUP_MOUNTPOINT");
    std::string profile_root = env_get(env, "PROFILE_ROOT", "/etc/btrfs-backup");
    std::string sources_dir = env_get(env, "PROFILE_SOURCES_DIR");
    if (sources_dir.empty()) {
        if (profile_root == "/etc/btrfs-backup") {
            sources_dir = "/etc/btrfs-backup/profiles/" + profile_id + "/sources.d";
        } else {
            sources_dir = profile_root + "/profiles/" + profile_id + "/sources.d";
        }
    }

    std::ifstream stream(sources_table);
    if (!stream) {
        throw ValidationError("cannot read sources table " + sources_table.string());
    }
    json sources = json::array();
    std::string line;
    while (std::getline(stream, line)) {
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (fields.size() == 6) {
            fields.insert(fields.begin() + 1, fields.at(0));
        }
        if (fields.size() != 7) {
            throw ValidationError("sources table must contain 6 or 7 tab-separated fields per line");
        }
        sources.push_back({
            {"id", fields.at(0)},
            {"name", fields.at(1)},
            {"enabled", true},
            {"subvolume", fields.at(2)},
            {"localSnapshotDir", fields.at(3)},
            {"remoteSubdir", fields.at(4)},
            {"remoteRetention", std::stoll(fields.at(5))},
            {"localRetention", std::stoll(fields.at(6))}
        });
    }

    return normalize_profile({
        {"schemaVersion", schema_version},
        {"profileId", profile_id},
        {"name", env_required(env, "PROFILE_NAME")},
        {"enabled", true},
        {"target", {
            {"device", env_required(env, "BACKUP_DEVICE")},
            {"luksUuid", env_required(env, "BACKUP_LUKS_UUID")},
            {"btrfsUuid", env_get(env, "BACKUP_BTRFS_UUID", "")},
            {"partitionUuid", env_get(env, "BACKUP_PARTITION_UUID", "")},
            {"serial", env_get(env, "BACKUP_SERIAL", "")},
            {"mapperName", env_required(env, "BACKUP_MAPPER_NAME")},
            {"mountPoint", mount}
        }},
        {"paths", {
            {"sourcesDir", sources_dir},
            {"remoteRoot", env_get(env, "REMOTE_ROOT", mount + "/snapshots")},
            {"incomingRoot", env_get(env, "INCOMING_ROOT", mount + "/.incoming")},
            {"stateDir", env_get(env, "STATE_DIR", "/var/lib/btrfs-backup")},
            {"statusRoot", env_get(env, "STATUS_ROOT", "/run/btrfs-backup/profiles")},
            {"historyRoot", env_get(env, "HISTORY_ROOT", "/var/lib/btrfs-backup/history")}
        }},
        {"settings", {
            {"dailyLimit", env_bool(env, "DAILY_LIMIT", false)},
            {"incrementalRequired", env_bool(env, "INCREMENTAL_REQUIRED", false)},
            {"keepFailedLocalSnapshot", env_bool(env, "KEEP_FAILED_LOCAL_SNAPSHOT", false)},
            {"autoEject", env_bool(env, "AUTO_EJECT", false)},
            {"remoteRetention", env_int(env, "RETENTION_COUNT", 30)},
            {"localRetention", env_int(env, "LOCAL_RETENTION_COUNT", env_int(env, "RETENTION_COUNT", 30))},
            {"minimumTargetFreeBytes", env_int(env, "MIN_TARGET_FREE_BYTES", 5LL * 1024 * 1024 * 1024)},
            {"minimumLocalFreeBytes", env_int(env, "MIN_LOCAL_FREE_BYTES", 1024LL * 1024 * 1024)}
        }},
        {"notifications", {
            {"enabled", env_bool(env, "NOTIFY_ENABLE", false)},
            {"user", env_get(env, "NOTIFY_USER", "")},
            {"method", env_get(env, "NOTIFY_METHOD", "auto")}
        }},
        {"sources", sources}
    });
}

std::string iso_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &tm);
    return buffer;
}

void render_tree(const json& profile, const fs::path& output_dir) {
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
    json public_profile = profile;
    public_profile["generatedAt"] = iso_now();
    atomic_write(output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / (profile_id + ".json"), dump_json(public_profile), 0644);
}

void save_tree(const json& profile, const fs::path& etc_root, const fs::path& udev_root, const fs::path& public_root) {
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
    json public_profile = profile;
    public_profile["generatedAt"] = iso_now();
    atomic_write(public_root / (profile_id + ".json"), dump_json(public_profile), 0644);
}

std::string arg_value(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        fail(option + " requires a value");
    }
    return argv[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backup-profile [--etc-root PATH] [--udev-root PATH] [--public-root PATH] COMMAND\n"
              << "\nCommands:\n"
              << "  compose --sources-table PATH --output PATH\n"
              << "  validate --file PATH\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  save --file PATH\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup {

int profile_tool_main(int argc, char** argv) {
    fs::path etc_root = std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : "/etc/btrfs-backup";
    fs::path udev_root = std::getenv("BTRFS_BACKUP_UDEV_ROOT") ? std::getenv("BTRFS_BACKUP_UDEV_ROOT") : "/etc/udev/rules.d";
    fs::path public_root = std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") ? std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") : "/var/lib/btrfs-backup/public/profiles";
    std::vector<std::string> rest;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--etc-root") {
                etc_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--udev-root") {
                udev_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--public-root") {
                public_root = arg_value(i, argc, argv, arg);
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                for (; i < argc; ++i) {
                    rest.emplace_back(argv[i]);
                }
                break;
            }
        }
        if (rest.empty()) {
            usage();
            return 2;
        }
        std::string command = rest[0];
        fs::path file;
        fs::path output_dir;
        fs::path sources_table;
        std::string profile_id = "default";
        for (std::size_t i = 1; i < rest.size(); ++i) {
            const std::string& arg = rest[i];
            if (arg == "--file" && i + 1 < rest.size()) {
                file = rest[++i];
            } else if (arg == "--output-dir" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--profile" && i + 1 < rest.size()) {
                profile_id = rest[++i];
            } else if (arg == "--output" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--sources-table" && i + 1 < rest.size()) {
                sources_table = rest[++i];
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                fail("unknown option: " + arg);
            }
        }

        if (command == "compose") {
            if (sources_table.empty()) fail("compose requires --sources-table");
            if (output_dir.empty()) fail("compose requires --output");
            atomic_write(output_dir, dump_json(profile_from_environment_sources(sources_table)), 0600);
        } else if (command == "validate") {
            if (file.empty()) fail("validate requires --file");
            std::cout << dump_json(normalize_profile(load_json_file(file)));
        } else if (command == "render") {
            if (file.empty()) fail("render requires --file");
            if (output_dir.empty()) fail("render requires --output-dir");
            output_dir = fs::absolute(output_dir).lexically_normal();
            if (output_dir == "/" || output_dir == "/etc" || output_dir == "/usr" || output_dir == "/var") {
                throw ValidationError("refusing unsafe output directory: " + output_dir.string());
            }
            std::error_code ec;
            fs::remove_all(output_dir, ec);
            json profile = normalize_profile(load_json_file(file));
            render_tree(profile, output_dir);
            std::cout << "Rendered profile " << profile.at("profileId").get<std::string>() << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty()) fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            json profile = normalize_profile(load_json_file(file));
            save_tree(profile, etc_root, udev_root, public_root);
            std::cout << "Saved profile " << profile.at("profileId").get<std::string>() << "\n";
        } else if (command == "show") {
            std::cout << dump_json(load_profile_by_id(etc_root, profile_id));
        } else if (command == "export") {
            if (output_dir.empty()) fail("export requires --output");
            json profile = load_profile_by_id(etc_root, profile_id);
            atomic_write(output_dir, dump_json(profile), 0600);
            std::cout << "Exported profile " << profile.at("profileId").get<std::string>() << " to " << output_dir << "\n";
        } else {
            fail("unknown command: " + command);
        }
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
    return 0;
}

} // namespace btrfsbackup
