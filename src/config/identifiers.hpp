#pragma once

#include <compare>
#include <string>

namespace btrfsbackup {

struct ProfileId {
    std::string value;

    ProfileId() = default;
    explicit ProfileId(std::string input);

    auto operator<=>(const ProfileId&) const = default;
};

struct RunId {
    std::string value;

    RunId() = default;
    explicit RunId(std::string input);

    auto operator<=>(const RunId&) const = default;
};

struct SourceId {
    std::string value;

    SourceId() = default;
    explicit SourceId(std::string input);

    auto operator<=>(const SourceId&) const = default;
};

void validate_identifier(const std::string& value, const std::string& field_name);
void validate_profile_id(const std::string& profile_id);
void validate_run_id(const std::string& run_id);

} // namespace btrfsbackup
