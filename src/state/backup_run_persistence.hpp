#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <backup/backup_run_event.hpp>
#include <config/json.hpp>
#include <state/status_writer.hpp>

namespace btrfsbackup {

std::string backup_run_action_kind_name(BackupRunActionKind kind);
std::string backup_run_event_kind_name(BackupRunEventKind kind);

Json build_backup_run_checkpoint_json(const BackupRunCheckpoint& checkpoint);
Json build_backup_run_event_json(const BackupRunEvent& event);

class JsonFileBackupRunCheckpointStore final : public IBackupRunCheckpointStore {
public:
    explicit JsonFileBackupRunCheckpointStore(std::filesystem::path profile_state_dir);

    void write_checkpoint(const BackupRunCheckpoint& checkpoint) override;

private:
    std::filesystem::path profile_state_dir_;
};

struct BackupRunStatusContext {
    std::filesystem::path status_root;
    std::filesystem::path history_root;
    std::string profile_name;
    int source_count = 0;
    std::string started_at;
    std::map<std::string, std::string> source_names;
    std::string target_name;
};

class StatusBackupRunEventSink final : public IBackupRunEventSink {
public:
    explicit StatusBackupRunEventSink(BackupRunStatusContext context);

    void on_backup_run_event(const BackupRunEvent& event) override;

private:
    BackupRunStatusContext context_;
    RunId run_id_;
    int last_overall_progress_ = -1;
};

} // namespace btrfsbackup
