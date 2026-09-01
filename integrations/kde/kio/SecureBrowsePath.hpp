// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace btrfsbackup::kde::kio {

class SecureBrowseFile final {
  public:
    SecureBrowseFile() = default;
    explicit SecureBrowseFile(int descriptor) noexcept;
    ~SecureBrowseFile() noexcept;

    SecureBrowseFile(const SecureBrowseFile&) = delete;
    SecureBrowseFile& operator=(const SecureBrowseFile&) = delete;
    SecureBrowseFile(SecureBrowseFile&& other) noexcept;
    SecureBrowseFile& operator=(SecureBrowseFile&& other) noexcept;

    [[nodiscard]] int descriptor() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;

  private:
    int descriptor_ = -1;
};

enum class BrowseEntryKind {
    RegularFile,
    Directory,
};

struct BrowseDirectoryEntry {
    std::string name;
    BrowseEntryKind kind;
    std::uintmax_t size;
    mode_t mode;
    std::int64_t modified_at;
};

class BrowseDirectoryLimitError final : public std::runtime_error {
  public:
    BrowseDirectoryLimitError();
};

[[nodiscard]] SecureBrowseFile open_browse_regular_file(
    const std::filesystem::path& root,
    const std::filesystem::path& relative
);
[[nodiscard]] SecureBrowseFile open_browse_regular_file(
    int root_descriptor,
    const std::filesystem::path& relative
);
[[nodiscard]] SecureBrowseFile open_browse_directory(
    const std::filesystem::path& root,
    const std::filesystem::path& relative
);
[[nodiscard]] SecureBrowseFile open_browse_directory(
    int root_descriptor,
    const std::filesystem::path& relative
);
[[nodiscard]] SecureBrowseFile open_browse_metadata(
    const std::filesystem::path& root,
    const std::filesystem::path& relative
);
[[nodiscard]] SecureBrowseFile open_browse_metadata(
    int root_descriptor,
    const std::filesystem::path& relative
);
[[nodiscard]] std::vector<BrowseDirectoryEntry> list_browse_directory(
    int descriptor,
    std::size_t maximum_entries
);

} // namespace btrfsbackup::kde::kio
