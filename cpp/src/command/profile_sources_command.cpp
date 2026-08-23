#include <btrfsbackup/command/profile_sources_command.hpp>

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_sources.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

void write_source_record(
    std::ostream& output,
    const std::string& name,
    const std::string& subvolume,
    const std::string& local_snapshot_dir,
    const std::string& remote_subdir,
    long long remote_retention,
    long long local_retention
) {
    output << name << '\n'
           << subvolume << '\n'
           << local_snapshot_dir << '\n'
           << remote_subdir << '\n'
           << remote_retention << '\n'
           << local_retention << '\n';
}

} // namespace

namespace btrfsbackup::command {

void profile_sources(const std::vector<std::string>& args, std::ostream& output) {
    fs::path profile_json;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--file") {
            profile_json = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown profile sources option: " + arg);
        }
    }

    if (profile_json.empty()) {
        throw ValidationError("profile sources requires --file");
    }

    for (const ProfileSource& source : btrfsbackup::profile_sources_from_json(load_json_file(profile_json))) {
        if (!source.enabled) {
            continue;
        }
        write_source_record(
            output,
            source.id,
            source.subvolume,
            source.local_snapshot_dir,
            source.remote_subdir,
            source.remote_retention,
            source.local_retention
        );
    }
}

} // namespace btrfsbackup::command
