// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <core/ManagerProtocol.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>
#include <state/document/TargetStatusDocumentCodec.hpp>

namespace btrfsbackup::daemon {

struct ManagerCapabilities {
    std::string interface_name;
    int api_major = manager_protocol::api_major;
    int api_minor = manager_protocol::api_minor;
    int profile_schema_version = manager_protocol::profile_schema_version;
    int public_status_schema_version = manager_protocol::public_status_schema_version;
    int history_schema_version = manager_protocol::history_schema_version;
    int device_state_schema_version = manager_protocol::device_state_schema_version;
    bool read_only = true;
    std::vector<std::string> features;
};

struct ProfileSourceSummary {
    std::string id;
    std::string name;
};

struct ProfileSummary {
    std::string profile_id;
    std::string name;
    bool enabled = true;
    std::string target_name;
    std::vector<ProfileSourceSummary> sources;
    bool configuration_valid = true;
    std::string configuration_error_code;
};

using PublicRunStatus = btrfsbackup::state::document::PublicRunStatusV3;

struct PublicStatusResponse {
    PublicRunStatus run;
    int source_index = 0;
    int source_count = 0;
    std::string started_at;
    std::string updated_at;
    std::string last_success_at;
    std::string last_attempt_at;
    std::string last_attempt_state;
};

struct SanitizedHistoryEntry {
    std::string state;
    std::string error_code;
    std::string source_name;
    std::string target_name;
    std::string started_at;
    std::string finished_at;
    int source_count = 0;
    int overall_progress = -1;
    std::uint64_t bytes_transferred = 0;
};

struct SanitizedHistoryPage {
    std::vector<SanitizedHistoryEntry> entries;
};

using TargetStatus = btrfsbackup::state::document::TargetStatusV1;

struct OperationResult {
    std::string operation;
    std::string operation_id;
    std::string profile_id;
    std::string run_id;
    bool accepted = true;
};

struct BrowseSessionInfo {
    std::string session_id;
    std::string profile_id;
    std::string expires_at;
    bool read_only = true;
};

struct BackupCoverage {
    std::string profile_id;
    std::string source_id;
    std::string relative_path;
};

} // namespace btrfsbackup::daemon
