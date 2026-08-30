// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KJob>

#include <memory>
#include <thread>

#include <core/Cancellation.hpp>
#include <restore/RestorePlan.hpp>

namespace btrfsbackup::kde::restore {

class RestoreJob final : public KJob {
    Q_OBJECT

  public:
    explicit RestoreJob(btrfsbackup::restore::RestorePlan plan, QObject* parent = nullptr);
    ~RestoreJob() override;
    void start() override;

  protected:
    bool doKill() override;

  private:
    void finish(bool success, QString error);

    btrfsbackup::restore::RestorePlan plan_;
    CancellationToken cancellation_;
    std::jthread worker_;
    bool started_ = false;
};

} // namespace btrfsbackup::kde::restore
