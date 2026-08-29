// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include <backup/model/backup_run_action.hpp>
#include <core/identifiers.hpp>
#include <core/error_code.hpp>

namespace btrfsbackup::backup {

enum class BackupRunEventKind {
    RunStarted,
    SourceStarted,
    ActionStarted,
    TransferProgress,
    ActionCompleted,
    ActionFailed,
    CheckpointWritten,
    SourceCompleted,
    TargetValidationCompleted,
    RunCompleted,
    RunFailed,
    RunCancelled,
};

enum class OperationKind {
    Backup,
    TargetValidation,
    Planning,
};

struct RunStarted {
    ProfileId profile_id;
    RunId run_id;
    OperationKind operation_kind = OperationKind::Backup;
};

struct SourceStarted {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
};

struct ActionStarted {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
    BackupRunActionKind action_kind;
};

enum class BackupTransferStage {
    Sizing,
    Transferring,
};

struct TransferProgress {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
    BackupTransferStage stage = BackupTransferStage::Transferring;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_transferred = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::string message;
};

struct ActionCompleted {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
    BackupRunActionKind action_kind;
};

struct ActionFailed {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
    BackupRunActionKind action_kind;
    std::optional<ErrorCode> error_code;
    std::string message;
};

struct CheckpointWritten {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
    BackupRunActionKind action_kind;
};

struct SourceCompleted {
    ProfileId profile_id;
    RunId run_id;
    SourceId source_id;
    int source_index = 0;
};

struct RunCompleted {
    ProfileId profile_id;
    RunId run_id;
};

struct TargetValidationCompleted {
    ProfileId profile_id;
    RunId run_id;
};

struct RunFailed {
    ProfileId profile_id;
    RunId run_id;
    ErrorCode error_code;
    std::string message;
    OperationKind operation_kind = OperationKind::Backup;
};

struct RunCancelled {
    ProfileId profile_id;
    RunId run_id;
    std::optional<SourceId> source_id;
    int source_index = 0;
    std::optional<BackupRunActionKind> action_kind;
    std::optional<ErrorCode> error_code;
    std::string message;
    OperationKind operation_kind = OperationKind::Backup;
};

using BackupRunEvent = std::variant<
    RunStarted,
    SourceStarted,
    ActionStarted,
    TransferProgress,
    ActionCompleted,
    ActionFailed,
    CheckpointWritten,
    SourceCompleted,
    TargetValidationCompleted,
    RunCompleted,
    RunFailed,
    RunCancelled>;

[[nodiscard]] inline BackupRunEventKind backup_run_event_kind(const BackupRunEvent& event) {
    return std::visit([](const auto& typed_event) {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, RunStarted>)
            return BackupRunEventKind::RunStarted;
        if constexpr (std::is_same_v<Event, SourceStarted>)
            return BackupRunEventKind::SourceStarted;
        if constexpr (std::is_same_v<Event, ActionStarted>)
            return BackupRunEventKind::ActionStarted;
        if constexpr (std::is_same_v<Event, TransferProgress>)
            return BackupRunEventKind::TransferProgress;
        if constexpr (std::is_same_v<Event, ActionCompleted>)
            return BackupRunEventKind::ActionCompleted;
        if constexpr (std::is_same_v<Event, ActionFailed>)
            return BackupRunEventKind::ActionFailed;
        if constexpr (std::is_same_v<Event, CheckpointWritten>)
            return BackupRunEventKind::CheckpointWritten;
        if constexpr (std::is_same_v<Event, SourceCompleted>)
            return BackupRunEventKind::SourceCompleted;
        if constexpr (std::is_same_v<Event, TargetValidationCompleted>)
            return BackupRunEventKind::TargetValidationCompleted;
        if constexpr (std::is_same_v<Event, RunCompleted>)
            return BackupRunEventKind::RunCompleted;
        if constexpr (std::is_same_v<Event, RunFailed>)
            return BackupRunEventKind::RunFailed;
        return BackupRunEventKind::RunCancelled;
    },
                      event);
}

[[nodiscard]] inline const ProfileId& backup_run_event_profile_id(const BackupRunEvent& event) {
    return std::visit([](const auto& typed_event) -> const ProfileId& {
        return typed_event.profile_id;
    },
                      event);
}

[[nodiscard]] inline const RunId& backup_run_event_run_id(const BackupRunEvent& event) {
    return std::visit([](const auto& typed_event) -> const RunId& {
        return typed_event.run_id;
    },
                      event);
}

[[nodiscard]] inline std::optional<SourceId> backup_run_event_source_id(const BackupRunEvent& event) {
    return std::visit([](const auto& typed_event) -> std::optional<SourceId> {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, RunStarted> || std::is_same_v<Event, TargetValidationCompleted> || std::is_same_v<Event, RunCompleted> || std::is_same_v<Event, RunFailed>) {
            return std::nullopt;
        } else if constexpr (std::is_same_v<Event, RunCancelled>) {
            return typed_event.source_id;
        } else {
            return typed_event.source_id;
        }
    },
                      event);
}

[[nodiscard]] inline int backup_run_event_source_index(const BackupRunEvent& event) {
    return std::visit([](const auto& typed_event) {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, RunStarted> || std::is_same_v<Event, TargetValidationCompleted> || std::is_same_v<Event, RunCompleted> || std::is_same_v<Event, RunFailed>) {
            return 0;
        } else {
            return typed_event.source_index;
        }
    },
                      event);
}

[[nodiscard]] inline std::optional<BackupRunActionKind> backup_run_event_action_kind(
    const BackupRunEvent& event
) {
    return std::visit([](const auto& typed_event) -> std::optional<BackupRunActionKind> {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, ActionStarted> || std::is_same_v<Event, ActionCompleted> || std::is_same_v<Event, ActionFailed> || std::is_same_v<Event, CheckpointWritten>) {
            return typed_event.action_kind;
        } else if constexpr (std::is_same_v<Event, TransferProgress>) {
            return BackupRunActionKind::SendReceive;
        } else if constexpr (std::is_same_v<Event, RunCancelled>) {
            return typed_event.action_kind;
        } else {
            return std::nullopt;
        }
    },
                      event);
}

[[nodiscard]] inline OperationKind backup_run_event_operation_kind(const BackupRunEvent& event) {
    return std::visit([](const auto& typed_event) {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, RunStarted> || std::is_same_v<Event, RunFailed> || std::is_same_v<Event, RunCancelled>) {
            return typed_event.operation_kind;
        } else if constexpr (std::is_same_v<Event, TargetValidationCompleted>) {
            return OperationKind::TargetValidation;
        } else {
            return OperationKind::Backup;
        }
    },
                      event);
}

} // namespace btrfsbackup::backup
