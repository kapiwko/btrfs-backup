#pragma once

#include <cstddef>
#include <sys/types.h>

namespace btrfsbackup::platform_linux {

ssize_t splice_pipe(int source_fd, int target_fd, std::size_t max_bytes);

} // namespace btrfsbackup::platform_linux
