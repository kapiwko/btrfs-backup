#pragma once

#include <stdexcept>

namespace btrfsbackup {

struct ValidationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace btrfsbackup
