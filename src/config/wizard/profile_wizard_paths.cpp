#include <config/wizard/profile_wizard_paths.hpp>

#include <unistd.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace btrfsbackup::wizard {

std::filesystem::path default_output_dir() {
    if (geteuid() == 0) {
        return "/etc/btrfs-backup/generated";
    }
    return fs::current_path() / "generated";
}

} // namespace btrfsbackup::wizard
