#include <btrfsbackup/shell_env.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>

#include <btrfsbackup/process.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

std::map<std::string, std::string> read_shell_environment(const fs::path& path) {
    std::string script = "set -a; source \"$1\"; /usr/bin/env -0";
    std::string output = run_capture({"bash", "-c", script, "bash", path.string()});
    std::map<std::string, std::string> env;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\0', start);
        if (end == std::string::npos) {
            end = output.size();
        }
        std::string item = output.substr(start, end - start);
        std::size_t eq = item.find('=');
        if (eq != std::string::npos) {
            env[item.substr(0, eq)] = item.substr(eq + 1);
        }
        start = end + 1;
    }
    return env;
}

} // namespace btrfsbackup
