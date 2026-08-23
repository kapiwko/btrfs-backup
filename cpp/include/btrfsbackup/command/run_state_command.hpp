#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void check_last_success(const std::vector<std::string>& args, std::ostream& output);
void write_success_state(const std::vector<std::string>& args);
void migrate_legacy_state(const std::vector<std::string>& args);
void write_pending_marker(const std::vector<std::string>& args);
void read_pending_marker(const std::vector<std::string>& args, std::ostream& output);
void clear_pending_marker(const std::vector<std::string>& args);

} // namespace btrfsbackup::command
