#pragma once

#include <filesystem>
#include <sys/types.h>

#include <config/json.hpp>
#include <state/run_status.hpp>

namespace btrfsbackup {

Json build_status_json(const RunStatus& status);
std::string dump_status_json(const RunStatus& status);
Json build_public_status_json(const RunStatus& status);
std::string dump_public_status_json(const RunStatus& status);

void write_current_status(
    const std::filesystem::path& status_root,
    const RunStatus& status,
    mode_t mode = 0644
);

void write_history_entry(
    const std::filesystem::path& history_root,
    const RunStatus& status
);

} // namespace btrfsbackup
