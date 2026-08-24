#include <btrfsbackup/application/runtime_adapters.hpp>

#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>

#include <btrfsbackup/model/errors.hpp>
#include <btrfsbackup/system/process.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

CommandResult SystemCommandRunner::run(const std::vector<std::string>& argv) {
    return run_command(argv);
}

CommandResult SystemCommandRunner::run_controlled(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
) {
    return run_controlled_command(argv, options);
}

std::string capture_command(ICommandRunner& runner, const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }
    CommandResult result = runner.run(argv);
    if (result.exit_code != 0) {
        throw ValidationError("command failed: " + argv.front());
    }
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    return result.output;
}

bool StdFileSystemEffects::exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

bool StdFileSystemEffects::is_directory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

void StdFileSystemEffects::create_directories(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        throw ValidationError("could not create directory " + path.string() + ": " + ec.message());
    }
}

void StdFileSystemEffects::remove_file(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        throw ValidationError("could not remove file " + path.string() + ": " + ec.message());
    }
}

void StdFileSystemEffects::remove_directory(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        throw ValidationError("could not remove directory " + path.string() + ": " + ec.message());
    }
}

void StdFileSystemEffects::remove_tree(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        throw ValidationError("could not remove path tree " + path.string() + ": " + ec.message());
    }
}

void StdFileSystemEffects::rename_path(const fs::path& source, const fs::path& target) {
    std::error_code ec;
    fs::rename(source, target, ec);
    if (ec) {
        throw ValidationError("could not rename " + source.string() + " to " + target.string() + ": " + ec.message());
    }
}

std::vector<fs::path> StdFileSystemEffects::list_directory(const fs::path& path) {
    std::vector<fs::path> entries;
    std::error_code ec;
    if (!fs::is_directory(path, ec) || ec) {
        return entries;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(path, ec)) {
        if (ec) {
            throw ValidationError("could not list directory " + path.string() + ": " + ec.message());
        }
        entries.push_back(entry.path());
    }
    if (ec) {
        throw ValidationError("could not list directory " + path.string() + ": " + ec.message());
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

} // namespace btrfsbackup
