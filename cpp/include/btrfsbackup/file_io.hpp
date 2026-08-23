#pragma once

#include <filesystem>
#include <string>
#include <sys/types.h>

namespace btrfsbackup {

void atomic_write(const std::filesystem::path& path, const std::string& data, mode_t mode);

} // namespace btrfsbackup
