// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <backup/model/backup_run_event.hpp>
#include <core/durable_file_operations.hpp>
#include <state/status_writer.hpp>

namespace btrfsbackup::state {

struct BackupRunStatusContext {
    std::filesystem::path status_root;
    std::filesystem::path history_root;
    std::string profile_name;
    int source_count = 0;
    std::string started_at;
    std::map<std::string, std::string> source_names;
    std::string target_name;
};

class RunStatusProjection final : public btrfsbackup::backup::IBackupRunEventSink {
  public:
    RunStatusProjection(IDurableFileOperations& files, BackupRunStatusContext context);

    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) override;

  private:
    IDurableFileOperations& files_;
    BackupRunStatusContext context_;
    std::optional<RunId> run_id_;
    int last_overall_progress_ = -1;
};

} // namespace btrfsbackup::state
