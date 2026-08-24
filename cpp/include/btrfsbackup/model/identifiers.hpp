#pragma once

#include <string>

namespace btrfsbackup {

struct ProfileId {
    std::string value;

    explicit ProfileId(std::string input);
};

struct RunId {
    std::string value;

    explicit RunId(std::string input);
};

void validate_identifier(const std::string& value, const std::string& field_name);
void validate_profile_id(const std::string& profile_id);
void validate_run_id(const std::string& run_id);

} // namespace btrfsbackup
