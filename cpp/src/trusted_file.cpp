#include <btrfsbackup/trusted_file.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <btrfsbackup/errors.hpp>

namespace btrfsbackup {

void assert_trusted_config_file(const std::filesystem::path& path, const TrustedFilePolicy& policy) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        throw ValidationError("Configuration file does not exist or is not a regular file: " + path.string());
    }

    if (access(path.c_str(), R_OK) != 0) {
        throw ValidationError("Configuration file is not readable: " + path.string());
    }

    uid_t current_uid = geteuid();
    if (info.st_uid != 0 && !(policy.allow_current_user_owner && info.st_uid == current_uid)) {
        throw ValidationError("Trusted profile JSON must be owned by root: " + path.string());
    }

    if ((info.st_mode & 0077) != 0) {
        throw ValidationError("Trusted profile JSON must not be accessible by group or others: " + path.string());
    }
}

} // namespace btrfsbackup
