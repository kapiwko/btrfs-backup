#include <btrfsbackup/identifiers.hpp>

#include <regex>
#include <string>
#include <utility>

#include <btrfsbackup/errors.hpp>

namespace {

const std::regex identifier_re{"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"};
const std::regex run_id_re{"^[A-Za-z0-9][A-Za-z0-9._:-]*$"};

} // namespace

namespace btrfsbackup {

ProfileId::ProfileId(std::string input) : value(std::move(input)) {
    validate_profile_id(value);
}

RunId::RunId(std::string input) : value(std::move(input)) {
    validate_run_id(value);
}

void validate_identifier(const std::string& value, const std::string& field_name) {
    if (!std::regex_match(value, identifier_re)) {
        throw ValidationError(field_name + " contains unsupported characters");
    }
}

void validate_profile_id(const std::string& profile_id) {
    try {
        validate_identifier(profile_id, "profileId");
    } catch (const ValidationError&) {
        throw ValidationError("invalid profile id: " + profile_id);
    }
}

void validate_run_id(const std::string& run_id) {
    if (!std::regex_match(run_id, run_id_re)) {
        throw ValidationError("invalid run id: " + run_id);
    }
}

} // namespace btrfsbackup
