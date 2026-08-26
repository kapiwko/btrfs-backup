// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <backup/backup_run_event.hpp>
#include <state/status_writer.hpp>

namespace btrfsbackup {

struct BackupRunStatusContext {
    std::filesystem::path status_root;
    std::filesystem::path history_root;
    std::string profile_name;
    int source_count = 0;
    std::string started_at;
    std::map<std::string, std::string> source_names;
    std::string target_name;
};

class RunStatusProjection final : public IBackupRunEventSink {
  public:
    explicit RunStatusProjection(BackupRunStatusContext context);

    void on_backup_run_event(const BackupRunEvent& event) override;

  private:
    BackupRunStatusContext context_;
    std::optional<RunId> run_id_;
    int last_overall_progress_ = -1;
};

} // namespace btrfsbackup
