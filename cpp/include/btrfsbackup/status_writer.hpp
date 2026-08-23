#pragma once

#include <filesystem>
#include <string>
#include <sys/types.h>

#include <btrfsbackup/json.hpp>

namespace btrfsbackup {

struct StatusRecord {
    std::string profile_id;
    std::string profile_name;
    std::string run_id;
    std::string state;
    std::string phase;
    std::string message;
    std::string current_source_name;
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
    int exit_code = 0;
};

Json build_status_json(const StatusRecord& record);
std::string dump_status_json(const StatusRecord& record);

void write_current_status(
    const std::filesystem::path& status_root,
    const StatusRecord& record,
    mode_t mode = 0644
);

void write_history_entry(
    const std::filesystem::path& history_root,
    const StatusRecord& record,
    mode_t mode = 0644
);

} // namespace btrfsbackup
