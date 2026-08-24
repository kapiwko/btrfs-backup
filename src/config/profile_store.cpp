#include <config/profile_store.hpp>

#include <sys/random.h>
#include <sys/types.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <config/application_config.hpp>
#include <config/application_paths.hpp>
#include <config/errors.hpp>
#include <config/json.hpp>
#include <config/json_io.hpp>
#include <config/profile.hpp>
#include <config/profile_render.hpp>
#include <platform/linux/file_io.hpp>
#include <platform/linux/file_lock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

struct ConfigurationArtifact {
    fs::path destination;
    fs::path staged;
    fs::path previous;
    std::string content;
    mode_t mode;
    bool had_previous = false;
    bool published = false;
};

std::string new_configuration_generation() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ValidationError("cannot generate configuration generation");
        }
        offset += static_cast<std::size_t>(count);
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

Profile installed_profile(const Profile& profile) {
    Profile result = profile;
    result.configuration_generation = new_configuration_generation();
    return result;
}

Json public_profile_json(const Profile& profile) {
    Json sources = Json::array();
    for (const auto& source : profile.sources) {
        sources.push_back({{"id", source.id}, {"name", source.name}});
    }
    Json result = {
        {"schemaVersion", 1},
        {"profileId", profile.id},
        {"name", profile.name},
        {"target", {{"name", profile.target.mapper_name}}},
        {"sources", std::move(sources)},
    };
    if (!profile.configuration_generation.empty()) {
        result["configurationGeneration"] = profile.configuration_generation;
    }
    return result;
}

fs::path transaction_path(const fs::path& destination, const std::string& kind, const std::string& generation) {
    return destination.parent_path()
        / ("." + destination.filename().string() + "." + kind + "-" + generation);
}

void remove_if_present(const fs::path& path) noexcept {
    std::error_code error;
    fs::remove(path, error);
}

void rename_checked(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::rename(from, to, error);
    if (error) {
        throw ValidationError("cannot rename " + from.string() + " to " + to.string() + ": " + error.message());
    }
}

void validate_destination(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return;
    }
    if (error) {
        throw ValidationError("cannot inspect configuration destination " + path.string() + ": " + error.message());
    }
    if (fs::is_symlink(status) || !fs::is_regular_file(status)) {
        throw ValidationError("configuration destination is not a regular file: " + path.string());
    }
}

void stage_artifact(ConfigurationArtifact& artifact, const std::string& generation) {
    validate_destination(artifact.destination);
    artifact.staged = transaction_path(artifact.destination, "stage", generation);
    artifact.previous = transaction_path(artifact.destination, "previous", generation);
    remove_if_present(artifact.staged);
    remove_if_present(artifact.previous);
    atomic_write(artifact.staged, artifact.content, artifact.mode);
}

void publish_artifact(ConfigurationArtifact& artifact) {
    validate_destination(artifact.destination);
    std::error_code error;
    artifact.had_previous = fs::exists(artifact.destination, error);
    if (error) {
        throw ValidationError("cannot inspect configuration destination " + artifact.destination.string());
    }
    if (artifact.had_previous) {
        rename_checked(artifact.destination, artifact.previous);
    }
    try {
        rename_checked(artifact.staged, artifact.destination);
        artifact.published = true;
        fsync_dir(artifact.destination.parent_path());
    } catch (...) {
        if (artifact.had_previous) {
            std::error_code restore_error;
            fs::rename(artifact.previous, artifact.destination, restore_error);
        }
        throw;
    }
}

void rollback_artifacts(std::vector<ConfigurationArtifact>& artifacts) noexcept {
    for (auto it = artifacts.rbegin(); it != artifacts.rend(); ++it) {
        std::error_code error;
        if (it->published) {
            fs::remove(it->destination, error);
            error.clear();
        }
        if (it->had_previous) {
            fs::rename(it->previous, it->destination, error);
        }
        try {
            fsync_dir(it->destination.parent_path());
        } catch (...) {
        }
        remove_if_present(it->staged);
        remove_if_present(it->previous);
    }
}

void finish_artifacts(std::vector<ConfigurationArtifact>& artifacts) noexcept {
    for (ConfigurationArtifact& artifact : artifacts) {
        remove_if_present(artifact.staged);
        remove_if_present(artifact.previous);
    }
}

fs::path configuration_lock_path(const fs::path& etc_root, const std::string& profile_id) {
    if (fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup")) {
        return profile_lock_path(default_lock_root(), profile_id);
    }
    return profile_lock_path(etc_root / ".locks", profile_id);
}

} // namespace

void render_tree(const Profile& profile, const fs::path& output_dir) {
    const Profile rendered = installed_profile(profile);
    fs::path root = output_dir / "etc" / "btrfs-backup";
    atomic_write(root / "profiles" / rendered.id / "profile.json", dump_json(profile_to_json(rendered)), 0600);
    atomic_write(
        output_dir / "etc" / "udev" / "rules.d" / ("99-btrfs-backup-" + rendered.id + ".rules"),
        render_udev(rendered),
        0644
    );
    atomic_write(
        output_dir / "etc" / "systemd" / "system" / ("btrfs-backup@" + rendered.id + ".service.d") / "target-mount.conf",
        render_mount_dependency(rendered),
        0644
    );
    atomic_write(
        output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / (rendered.id + ".json"),
        dump_json(public_profile_json(rendered)),
        0644
    );
}

void save_tree(
    const Profile& profile,
    const fs::path& etc_root,
    const fs::path& udev_root,
    const fs::path& systemd_root,
    const fs::path& public_root,
    const std::function<void()>& activate
) {
    const Profile installed = installed_profile(profile);
    const std::string& generation = installed.configuration_generation;
    ApplicationConfig application_config = ApplicationConfig::load(etc_root);
    const fs::path source_root = profile_sources_dir(application_config.paths(), installed.id);
    const fs::path source_backup = source_root.parent_path()
        / (source_root.filename().string() + ".backup-" + generation);

    std::vector<ConfigurationArtifact> artifacts{
        {
            .destination = udev_root / ("99-btrfs-backup-" + installed.id + ".rules"),
            .staged = {},
            .previous = {},
            .content = render_udev(installed),
            .mode = 0644,
        },
        {
            .destination = systemd_root / ("btrfs-backup@" + installed.id + ".service.d") / "target-mount.conf",
            .staged = {},
            .previous = {},
            .content = render_mount_dependency(installed),
            .mode = 0644,
        },
        {
            .destination = etc_root / "profiles" / installed.id / "profile.json",
            .staged = {},
            .previous = {},
            .content = dump_json(profile_to_json(installed)),
            .mode = 0600,
        },
        {
            .destination = public_root / (installed.id + ".json"),
            .staged = {},
            .previous = {},
            .content = dump_json(public_profile_json(installed)),
            .mode = 0644,
        },
    };

    try {
        for (ConfigurationArtifact& artifact : artifacts) {
            stage_artifact(artifact, generation);
        }

        Profile staged_profile = profile_from_json(
            load_json_file(artifacts[2].staged),
            application_config.paths().target_mount_root
        );
        Json staged_public = load_json_file(artifacts[3].staged);
        if (staged_profile.configuration_generation != generation
            || staged_public.value("configurationGeneration", "") != generation) {
            throw ValidationError("staged configuration generation mismatch");
        }

        FileLock lock(configuration_lock_path(etc_root, installed.id));
        if (!lock.try_acquire()) {
            throw ValidationError("profile is active; configuration save refused: " + installed.id);
        }

        bool source_backed_up = false;
        bool activation_attempted = false;
        try {
            std::error_code source_error;
            if (fs::exists(source_root, source_error) && !source_error) {
                rename_checked(source_root, source_backup);
                source_backed_up = true;
            } else if (source_error) {
                throw ValidationError("cannot inspect legacy source configuration: " + source_error.message());
            }

            for (std::size_t index = 0; index < artifacts.size() - 1; ++index) {
                publish_artifact(artifacts[index]);
            }
            if (activate) {
                activation_attempted = true;
                activate();
            }
            publish_artifact(artifacts.back());
        } catch (...) {
            rollback_artifacts(artifacts);
            if (source_backed_up) {
                std::error_code restore_error;
                fs::rename(source_backup, source_root, restore_error);
            }
            if (activation_attempted) {
                try {
                    activate();
                } catch (...) {
                }
            }
            throw;
        }
        finish_artifacts(artifacts);
    } catch (...) {
        finish_artifacts(artifacts);
        throw;
    }
}

} // namespace btrfsbackup
