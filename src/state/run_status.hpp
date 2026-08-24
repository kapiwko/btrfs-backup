#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace btrfsbackup {

enum class RunState {
    Running,
    Succeeded,
    Failed,
    Cancelled,
    Skipped,
};

enum class RunPhase {
    RunStarted,
    SourceStarted,
    RecoverPending,
    CleanupIncoming,
    BeforeSnapshotHook,
    CreateSnapshot,
    AfterSnapshotHook,
    SelectParent,
    SendReceive,
    Transferring,
    VerifyReceived,
    CommitReceived,
    ApplyRemoteRetention,
    ApplyLocalRetention,
    CleanupSource,
    SourceCompleted,
    Succeeded,
    Cancelled,
    Skipped,
    ValidatingTarget,
};

enum class ProgressAccuracy {
    Indeterminate,
    Estimated,
};

struct ErrorCode {
    std::string value;
};

struct SuggestedAction {
    std::string value;
};

using RunDetailValue = std::variant<bool, std::int64_t, std::uint64_t, std::string>;
using RunDetails = std::map<std::string, RunDetailValue>;

struct RunProgress {
    std::uint64_t processed_bytes = 0;
    std::optional<std::uint64_t> estimated_bytes;
    std::uint64_t run_processed_bytes = 0;
    std::uint64_t speed_bps = 0;
    std::optional<int> eta_seconds;
    std::optional<int> source_percent;
    std::optional<int> overall_percent;
    ProgressAccuracy accuracy = ProgressAccuracy::Indeterminate;
};

struct RunError {
    ErrorCode code;
    std::string message;
    bool recoverable = false;
    SuggestedAction suggested_action;
};

struct RunStatus {
    std::string profile_id;
    std::string profile_name;
    std::string run_id;
    RunState state = RunState::Running;
    RunPhase phase = RunPhase::RunStarted;
    std::string message;
    std::string current_source_name;
    std::string target_name;
    int source_index = 0;
    int source_count = 0;
    std::string started_at;
    std::string updated_at;
    std::string finished_at;
    std::optional<RunError> error;
    RunDetails details;
    bool can_cancel = false;
    RunProgress progress;
    int exit_code = 0;
};

std::string run_state_name(RunState state);
std::string run_phase_name(RunPhase phase);
std::string progress_accuracy_name(ProgressAccuracy accuracy);

} // namespace btrfsbackup
