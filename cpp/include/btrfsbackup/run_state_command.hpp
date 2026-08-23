#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup {

void command_check_last_success(const std::vector<std::string>& args, std::ostream& output);
void command_write_success_state(const std::vector<std::string>& args);
void command_migrate_legacy_state(const std::vector<std::string>& args);
void command_write_pending_marker(const std::vector<std::string>& args);
void command_read_pending_marker(const std::vector<std::string>& args, std::ostream& output);
void command_clear_pending_marker(const std::vector<std::string>& args);

} // namespace btrfsbackup
