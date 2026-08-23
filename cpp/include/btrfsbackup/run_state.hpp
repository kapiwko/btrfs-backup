#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup {

struct SuccessState {
    std::string date;
    std::string timestamp;
    std::string run_id;
    std::string profile_id;
    std::string profile_name;
    int source_count = 0;
    std::string target_luks_uuid;
    std::string config_fingerprint;
};

bool last_success_matches(
    const std::filesystem::path& profile_state_dir,
    const std::string& today,
    const std::string& target_luks_uuid,
    const std::string& config_fingerprint
);

void write_success_state(const std::filesystem::path& profile_state_dir, const SuccessState& state);

void command_check_last_success(const std::vector<std::string>& args, std::ostream& output);
void command_write_success_state(const std::vector<std::string>& args);

} // namespace btrfsbackup
