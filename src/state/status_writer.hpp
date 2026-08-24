#pragma once

#include <filesystem>
#include <string>
#include <sys/types.h>

#include <config/json.hpp>

namespace btrfsbackup {

struct RunStatusRecord {
    std::string profile_id;
    std::string profile_name;
    std::string run_id;
    std::string state;
    std::string phase;
    std::string message;
    std::string current_source_name;
    std::string target_name;
    int source_index = 0;
    int source_count = 0;
    std::string started_at;
    std::string updated_at;
    std::string finished_at;
    std::string error_code;
    std::string error_message;
    Json details = Json::object();
    bool recoverable = false;
    std::string suggested_action;
    bool can_cancel = false;
    std::uint64_t bytes_processed = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_processed = 0;
    std::uint64_t speed_bps = 0;
    int eta_seconds = -1;
    int source_progress = -1;
    int overall_progress = -1;
    std::string progress_accuracy = "indeterminate";
    int exit_code = 0;
};

Json build_status_json(const RunStatusRecord& record);
std::string dump_status_json(const RunStatusRecord& record);
Json build_public_status_json(const RunStatusRecord& record);
std::string dump_public_status_json(const RunStatusRecord& record);

void write_current_status(
    const std::filesystem::path& status_root,
    const RunStatusRecord& record,
    mode_t mode = 0644
);

void write_history_entry(
    const std::filesystem::path& history_root,
    const RunStatusRecord& record
);

} // namespace btrfsbackup
