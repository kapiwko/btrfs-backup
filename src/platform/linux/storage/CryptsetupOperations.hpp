// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::storage {

[[nodiscard]] std::optional<std::string> luks_uuid_from_device_mapper_uuid(std::string_view value);
[[nodiscard]] std::string active_luks_uuid_from_device_mapper(const std::string& mapper);

class ActiveDeviceUnavailableError final : public btrfsbackup::ValidationError {
  public:
    using ValidationError::ValidationError;
};

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
    [[nodiscard]] virtual std::string active_luks_uuid(const std::string& mapper) = 0;
    [[nodiscard]] virtual std::filesystem::path active_device(const std::string& mapper) = 0;
    [[nodiscard]] virtual std::string format_luks2(const std::filesystem::path& device, int key_fd) = 0;
    virtual void open_luks2(const std::filesystem::path& device, const std::string& mapper, int key_fd) = 0;
    virtual void open_luks2_read_only(
        const std::filesystem::path& device,
        const std::string& mapper,
        int key_fd
    ) = 0;
    virtual void close(const std::string& mapper) = 0;
};

class CryptsetupOperations final : public ICryptsetupOperations {
  public:
    [[nodiscard]] LuksHeader inspect_luks2(const std::filesystem::path& device) override;
    void add_key(const std::filesystem::path& device, int authorization_fd, int new_key_fd) override;
    void test_key(const std::filesystem::path& device, int key_fd) override;
    void remove_keyslot(const std::filesystem::path& device, int keyslot, int authorization_fd) override;
    [[nodiscard]] std::string active_luks_uuid(const std::string& mapper) override;
    [[nodiscard]] std::filesystem::path active_device(const std::string& mapper) override;
    [[nodiscard]] std::string format_luks2(const std::filesystem::path& device, int key_fd) override;
    void open_luks2(const std::filesystem::path& device, const std::string& mapper, int key_fd) override;
    void open_luks2_read_only(
        const std::filesystem::path& device,
        const std::string& mapper,
        int key_fd
    ) override;
    void close(const std::string& mapper) override;
};

} // namespace btrfsbackup::platform::linux::storage
