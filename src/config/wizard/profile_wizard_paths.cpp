#include <config/wizard/profile_wizard_paths.hpp>

#include <unistd.h>

#include <filesystem>
#include <set>
#include <string>

#include <config/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::wizard {

std::filesystem::path default_output_dir() {
    if (geteuid() == 0) {
        return "/etc/btrfs-backup/generated";
    }
    return fs::current_path() / "generated";
}

void assert_safe_output_dir(const fs::path& output_dir) {
    fs::path normalized = fs::absolute(output_dir).lexically_normal();
    static const std::set<std::string> unsafe{"/", "/etc", "/usr", "/var", "/home", "/root"};
    if (unsafe.count(normalized.string()) > 0) {
        throw ValidationError("refusing unsafe output directory: " + normalized.string());
    }
}

} // namespace btrfsbackup::wizard
