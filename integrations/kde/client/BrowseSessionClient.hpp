// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ManagerApi.hpp"

#include <optional>

#include <QDBusUnixFileDescriptor>

namespace btrfsbackup::kde {

class BrowseSessionClient {
  public:
    explicit BrowseSessionClient(QDBusConnection bus = QDBusConnection::systemBus());

    [[nodiscard]] std::optional<BrowseSessionInfo> open(const QString& profile_id) const;
    [[nodiscard]] std::optional<BrowseSessionInfo> renew(const QString& session_id) const;
    [[nodiscard]] bool setActive(const QString& session_id, bool active) const;
    [[nodiscard]] bool close(const QString& session_id) const;
    [[nodiscard]] std::optional<QString> listDirectory(const QString& session_id, const QString& path) const;
    [[nodiscard]] std::optional<QString> listDirectoryPage(
        const QString& session_id,
        const QString& path,
        const QString& continuation_token,
        uint limit
    ) const;
    [[nodiscard]] std::optional<QString> inspectEntry(const QString& session_id, const QString& path) const;
    [[nodiscard]] std::optional<QString> inspectRepository(const QString& session_id) const;
    [[nodiscard]] QDBusUnixFileDescriptor openRoot(const QString& session_id) const;
    [[nodiscard]] const QString& lastErrorName() const noexcept;

  private:
    [[nodiscard]] std::optional<QString> payload(QDBusPendingCall call) const;

    ManagerClient manager_;
    mutable QString last_error_name_;
};

} // namespace btrfsbackup::kde
