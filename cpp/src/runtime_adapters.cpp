#include <btrfsbackup/runtime_adapters.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/process.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

CommandResult SystemCommandRunner::run(const std::vector<std::string>& argv) {
    return run_command(argv);
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

void StdFileSystemEffects::rename_path(const fs::path& source, const fs::path& target) {
    std::error_code ec;
    fs::rename(source, target, ec);
    if (ec) {
        throw ValidationError("could not rename " + source.string() + " to " + target.string() + ": " + ec.message());
    }
}

} // namespace btrfsbackup
