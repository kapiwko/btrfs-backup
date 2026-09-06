// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BrowseSessionClient.hpp"
#include "ManagerApi.hpp"
#include "RepositorySnapshotModel.hpp"
#include "SecureBrowsePath.hpp"

#include <QHash>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace btrfsbackup::kde::kio {

struct RemoteEntry {
    QString name;
    bool directory = false;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t modified_at = 0;
};

struct RemoteDirectoryPage {
    std::vector<RemoteEntry> entries;
    QString continuation_token;
};

class BrowseRepositoryClient final {
  public:
    [[nodiscard]] std::optional<QList<ProfileSummary>> profiles();
    [[nodiscard]] std::optional<QHash<QString, RepositorySnapshot>> snapshots(const QString& session_id);
    [[nodiscard]] std::optional<RemoteDirectoryPage> directoryPage(
        const QString& session_id,
        const QString& path,
        const QString& continuation_token
    );
    [[nodiscard]] std::optional<RemoteEntry> entry(const QString& session_id, const QString& path);
    [[nodiscard]] std::optional<PreviousVersionsPage> previousVersions(
        const QString& session_id,
        const QString& profile_id,
        const QString& source_id,
        const QString& relative_path,
        const QString& continuation_token
    );
    [[nodiscard]] SecureBrowseFile openFile(const QString& session_id, const QString& path);
    [[nodiscard]] QString lastErrorName() const;

  private:
    QString last_error_name_;
};

class BrowseOperationPin final {
  public:
    explicit BrowseOperationPin(const QString& session_id);
    ~BrowseOperationPin() noexcept;
    BrowseOperationPin(const BrowseOperationPin&) = delete;
    BrowseOperationPin& operator=(const BrowseOperationPin&) = delete;
    BrowseOperationPin(BrowseOperationPin&&) = delete;
    BrowseOperationPin& operator=(BrowseOperationPin&&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    QString session_id_;
    std::optional<BrowseOperationLease> lease_;
};

} // namespace btrfsbackup::kde::kio
