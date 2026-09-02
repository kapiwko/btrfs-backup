// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::platform::linux::storage {

class IPartitionTableOperations {
  public:
    virtual ~IPartitionTableOperations() = default;
    virtual void replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor
    ) = 0;
};

class LibfdiskPartitionTableOperations final : public IPartitionTableOperations {
  public:
    void replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor
    ) override;
};

} // namespace btrfsbackup::platform::linux::storage
