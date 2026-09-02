// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::platform::linux::storage {

class ISignatureOperations {
  public:
    virtual ~ISignatureOperations() = default;
    virtual void wipe_all(
        const std::filesystem::path& device,
        const std::string& expected_major_minor
    ) = 0;
};

class LibblkidSignatureOperations final : public ISignatureOperations {
  public:
    void wipe_all(
        const std::filesystem::path& device,
        const std::string& expected_major_minor
    ) override;
};

} // namespace btrfsbackup::platform::linux::storage
