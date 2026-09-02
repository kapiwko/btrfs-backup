// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::platform::linux::storage {

struct BlockDeviceMetadata {
    std::string filesystem_type;
    std::string filesystem_uuid;
    std::string partition_uuid;
};

class IBlockDeviceMetadataReader {
  public:
    virtual ~IBlockDeviceMetadataReader() = default;
    [[nodiscard]] virtual BlockDeviceMetadata read(const std::filesystem::path& device) = 0;
};

class LibblkidBlockDeviceMetadataReader final : public IBlockDeviceMetadataReader {
  public:
    [[nodiscard]] BlockDeviceMetadata read(const std::filesystem::path& device) override;
};

} // namespace btrfsbackup::platform::linux::storage
