// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string_view>

#include <state/PersistentDocumentOperations.hpp>

namespace btrfsbackup::platform::linux {

class PosixDurableFileOperations final : public btrfsbackup::state::IPersistentDocumentOperations {
  public:
    void ensure_directory(
        const std::filesystem::path& path,
        std::filesystem::perms permissions
    ) override;
    void write_atomically(
        const std::filesystem::path& path,
        std::string_view data,
        std::filesystem::perms permissions
    ) override;
    void remove_durably(const std::filesystem::path& path) override;
};

} // namespace btrfsbackup::platform::linux
