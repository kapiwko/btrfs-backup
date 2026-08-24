#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace btrfsbackup {

struct ValidationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RecoveryRequiredError : ValidationError {
    RecoveryRequiredError(std::string error_code, const std::string& message)
        : ValidationError(message), error_code(std::move(error_code)) {
    }

    std::string error_code;
};

} // namespace btrfsbackup
