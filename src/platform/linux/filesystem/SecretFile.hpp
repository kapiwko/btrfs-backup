// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux::filesystem {

inline constexpr std::size_t maximum_secret_bytes = 4096;

[[nodiscard]] OwnedFileDescriptor copy_secret_to_sealed_file(
    int source_fd,
    std::size_t maximum_bytes = maximum_secret_bytes
);

[[nodiscard]] OwnedFileDescriptor create_sealed_secret_file(std::span<const std::byte> secret);
[[nodiscard]] OwnedFileDescriptor generate_random_secret_file(std::size_t size);
void install_secret_file(
    int source_fd,
    const std::filesystem::path& destination,
    std::size_t maximum_bytes = maximum_secret_bytes
);

} // namespace btrfsbackup::platform::linux::filesystem
