#pragma once

#include <filesystem>
#include <string>

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

struct PendingMarker {
    std::string source_name;
    std::string local_snapshot_path;
    std::string run_id;
    std::string timestamp;
};

bool last_success_matches(
    const std::filesystem::path& profile_state_dir,
    const std::string& today,
    const std::string& target_luks_uuid,
    const std::string& config_fingerprint
);

void write_success_state(const std::filesystem::path& profile_state_dir, const SuccessState& state);
std::filesystem::path cancel_request_path(const std::filesystem::path& profile_state_dir);
void write_cancel_request(const std::filesystem::path& profile_state_dir);
bool cancel_requested(const std::filesystem::path& profile_state_dir);
void clear_cancel_request(const std::filesystem::path& profile_state_dir);
std::filesystem::path pending_marker_path(const std::filesystem::path& profile_state_dir, const std::string& source_name);
void write_pending_marker(const std::filesystem::path& profile_state_dir, const PendingMarker& marker);
std::string read_pending_marker_field(const std::filesystem::path& marker_path, const std::string& field);
void clear_pending_marker(const std::filesystem::path& marker_path, const std::filesystem::path& profile_state_dir);

} // namespace btrfsbackup
