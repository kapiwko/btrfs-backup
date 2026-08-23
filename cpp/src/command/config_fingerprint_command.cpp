#include <btrfsbackup/command/config_fingerprint_command.hpp>

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <btrfsbackup/config_fingerprint.hpp>
#include <btrfsbackup/errors.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

} // namespace

namespace btrfsbackup::command {

void config_fingerprint(const std::vector<std::string>& args, std::ostream& output) {
    std::string version;
    fs::path config_file;
    std::vector<fs::path> source_files;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--version") {
            version = arg_value(args, i, arg);
        } else if (arg == "--config") {
            config_file = arg_value(args, i, arg);
        } else if (arg == "--source") {
            source_files.emplace_back(arg_value(args, i, arg));
        } else {
            throw ValidationError("unknown state fingerprint option: " + arg);
        }
    }

    if (version.empty()) {
        throw ValidationError("state fingerprint requires --version");
    }
    if (config_file.empty()) {
        throw ValidationError("state fingerprint requires --config");
    }

    output << btrfsbackup::compute_config_fingerprint(version, config_file, source_files) << '\n';
}

} // namespace btrfsbackup::command
