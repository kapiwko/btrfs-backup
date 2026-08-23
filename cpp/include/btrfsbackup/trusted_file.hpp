#pragma once

#include <filesystem>

namespace btrfsbackup {

struct TrustedFilePolicy {
    bool allow_current_user_owner = false;
};

void assert_trusted_config_file(const std::filesystem::path& path, const TrustedFilePolicy& policy = {});

} // namespace btrfsbackup
