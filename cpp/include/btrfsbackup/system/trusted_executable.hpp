#pragma once

#include <filesystem>

#include <btrfsbackup/system/safe_directory_root.hpp>

namespace btrfsbackup {

struct TrustedExecutablePolicy {
    bool allow_current_user_owner = false;
    bool verify_parent_directories = true;
};

SafeDirectoryHandle open_trusted_executable(
    const SafeDirectoryRoot& trusted_root,
    const std::filesystem::path& program,
    const TrustedExecutablePolicy& policy = {}
);

} // namespace btrfsbackup
