// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace btrfsbackup::daemon {

struct ManagerAuditRecord {
    std::uint32_t caller_uid = 0;
    std::string action;
    std::string profile_id;
    std::string result;
    std::string error_code;
};

class IManagerAuditLog {
  public:
    virtual ~IManagerAuditLog() = default;

    [[nodiscard]] virtual std::optional<std::string> write(
        const ManagerAuditRecord& record
    ) noexcept = 0;
};

class FileManagerAuditLog final : public IManagerAuditLog {
  public:
    explicit FileManagerAuditLog(const std::filesystem::path& path);
    ~FileManagerAuditLog() noexcept override;

    FileManagerAuditLog(const FileManagerAuditLog&) = delete;
    FileManagerAuditLog& operator=(const FileManagerAuditLog&) = delete;

    [[nodiscard]] std::optional<std::string> write(
        const ManagerAuditRecord& record
    ) noexcept override;

  private:
    std::filesystem::path path_;
    int descriptor_ = -1;
};

} // namespace btrfsbackup::daemon
