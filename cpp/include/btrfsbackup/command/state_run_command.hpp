#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::command {

void state_check_last_success(const std::vector<std::string>& args, std::ostream& output);
void state_write_success(const std::vector<std::string>& args);
void state_pending_write(const std::vector<std::string>& args);
void state_pending_read(const std::vector<std::string>& args, std::ostream& output);
void state_pending_clear(const std::vector<std::string>& args);

} // namespace btrfsbackup::command
