// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KJob>

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

#include <core/Cancellation.hpp>
#include <restore/RestorePlan.hpp>
#include <restore/RestoreError.hpp>

namespace btrfsbackup::kde::restore {

class RestoreJob final : public KJob {
    Q_OBJECT

  public:
    explicit RestoreJob(
        btrfsbackup::restore::RestorePlan plan,
        std::uint64_t total_bytes = 0,
        QString source_display_name = {},
        QObject* parent = nullptr
    );
    ~RestoreJob() noexcept override;
    void start() override;
    [[nodiscard]] bool hasRestoreError() const noexcept;
    [[nodiscard]] btrfsbackup::restore::RestoreErrorCode restoreErrorCode() const noexcept;
    [[nodiscard]] QString technicalDetails() const;
    [[nodiscard]] std::uint64_t restoredFiles() const noexcept;
    [[nodiscard]] std::uint64_t restoredBytes() const noexcept;

  signals:
    void phaseChanged(bool checking_space);
    void progressChanged(
        qulonglong files,
        qulonglong bytes,
        qulonglong bytes_per_second,
        const QString& current_name
    );

  protected:
    bool doKill() override;

  private:
    void finish_successfully();
    void finish_with_restore_error(btrfsbackup::restore::RestoreErrorCode code, QString details);
    void finish_with_unexpected_error(QString details);

    btrfsbackup::restore::RestorePlan plan_;
    QString source_display_name_;
    CancellationToken cancellation_;
    std::jthread worker_;
    std::optional<btrfsbackup::restore::RestoreErrorCode> restore_error_code_;
    QString technical_details_;
    std::uint64_t restored_files_ = 0;
    std::uint64_t restored_bytes_ = 0;
    std::uint64_t total_bytes_ = 0;
    bool started_ = false;
};

} // namespace btrfsbackup::kde::restore
