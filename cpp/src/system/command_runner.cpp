#include <btrfsbackup/system/command_runner.hpp>

#include <btrfsbackup/model/errors.hpp>

namespace btrfsbackup {

CommandResult PosixCommandRunner::run(const std::vector<std::string>& argv) {
    return run_command(argv);
}

CommandResult PosixCommandRunner::run_controlled(
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

} // namespace btrfsbackup
