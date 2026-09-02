// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <core/Identifiers.hpp>
#include <state/model/RunStatus.hpp>

namespace btrfsbackup::state {
namespace document {

enum class PublicRunState {
    Idle,
    Starting,
    Running,
    Validating,
    Validated,
    Skipped,
    Succeeded,
    Failed,
    Cancelled,
    Exited,
    Unavailable,
    Unknown,
};

enum class PublicActivity {
    Preparing,
    Sizing,
    Transferring,
    Finalizing,
    Idle,
    Unknown,
};

enum class PublicErrorCode {
    None,
    Failed,
    Cancelled,
};

struct ExtensibleValue {
    std::string value;
    bool known = true;
};

struct TransferProgress {
    std::uint64_t bytes_processed = 0;
    std::optional<std::uint64_t> bytes_total_estimated;
    std::uint64_t speed_bps = 0;
    std::optional<std::uint64_t> eta_seconds;
    std::optional<int> source_percent;
    std::optional<int> overall_percent;
    ProgressAccuracy accuracy = ProgressAccuracy::Indeterminate;
};

struct PublicRunStatusV3 {
    std::optional<RunId> run_id;
    PublicRunState state = PublicRunState::Unavailable;
    ExtensibleValue phase{"idle", true};
    PublicActivity activity = PublicActivity::Idle;
    bool can_cancel = false;
    PublicErrorCode error_code = PublicErrorCode::None;
    std::string source_name;
    std::string target_name;
    TransferProgress progress;
    std::string unknown_state;
    std::string unknown_activity;
};

struct PrivateRunHistoryV2 {
    ProfileId profile_id;
    std::string profile_name;
    RunId run_id;
    ExtensibleValue state;
    ExtensibleValue phase;
    std::string message;
    std::string current_source_name;
    std::string target_name;
    int source_index = 0;
    int source_count = 0;
    std::string started_at;
    std::string updated_at;
    std::string finished_at;
    std::string error_code;
    std::string error_message;
    RunDetails details;
    bool recoverable = false;
    std::string suggested_action;
    bool can_cancel = false;
    std::uint64_t bytes_processed = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_processed = 0;
    TransferProgress progress;
    int exit_code = 0;
};

class RunStatusDocumentCodec {
  public:
    [[nodiscard]] PublicRunStatusV3 parse_public(std::string_view document) const;
    [[nodiscard]] std::optional<PublicRunStatusV3> try_parse_public(std::string_view document) const noexcept;
    [[nodiscard]] PrivateRunHistoryV2 parse_private(std::string_view document) const;

    [[nodiscard]] std::string serialize_public(const PublicRunStatusV3& status) const;
    [[nodiscard]] std::string serialize_private(const PrivateRunHistoryV2& history) const;
};

[[nodiscard]] PublicRunStatusV3 make_public_status(const RunStatus& status);
[[nodiscard]] PrivateRunHistoryV2 make_private_history(const RunStatus& status);
[[nodiscard]] std::string public_run_state_name(const PublicRunStatusV3& status);
[[nodiscard]] std::string public_activity_name(const PublicRunStatusV3& status);
[[nodiscard]] std::string public_error_code_name(PublicErrorCode code);

} // namespace document
} // namespace btrfsbackup::state
