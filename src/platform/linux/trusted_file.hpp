#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

struct TrustedFilePolicy {
    bool allow_current_user_owner = false;
    bool allow_group_other_read = false;
};

void assert_trusted_config_file(const std::filesystem::path& path, const TrustedFilePolicy& policy = {});
std::string read_trusted_config_file(const std::filesystem::path& path, const TrustedFilePolicy& policy = {});

} // namespace btrfsbackup
