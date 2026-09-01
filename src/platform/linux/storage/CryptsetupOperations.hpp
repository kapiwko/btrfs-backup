// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <backup/ports/ICommandRunner.hpp>

namespace btrfsbackup::platform::linux::storage {

struct LuksHeader {
    std::string uuid;
    std::vector<int> keyslots;
};

class ICryptsetupOperations {
  public:
    virtual ~ICryptsetupOperations() = default;
    [[nodiscard]] virtual LuksHeader inspect_luks2(const std::filesystem::path& device) = 0;
    virtual void add_key(const std::filesystem::path& device, int authorization_fd, int new_key_fd) = 0;
    virtual void test_key(const std::filesystem::path& device, int key_fd) = 0;
    virtual void remove_keyslot(const std::filesystem::path& device, int keyslot, int authorization_fd) = 0;
};

class CryptsetupOperations final : public ICryptsetupOperations {
  public:
    explicit CryptsetupOperations(btrfsbackup::backup::ICommandRunner& commands);

    [[nodiscard]] LuksHeader inspect_luks2(const std::filesystem::path& device) override;
    void add_key(const std::filesystem::path& device, int authorization_fd, int new_key_fd) override;
    void test_key(const std::filesystem::path& device, int key_fd) override;
    void remove_keyslot(const std::filesystem::path& device, int keyslot, int authorization_fd) override;

  private:
    void require_success(
        const std::vector<std::string>& command,
        const btrfsbackup::backup::ControlledCommandOptions& options,
        const char* operation
    );

    btrfsbackup::backup::ICommandRunner& commands_;
};

} // namespace btrfsbackup::platform::linux::storage
