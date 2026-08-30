// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <core/ManagerProtocol.hpp>

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
    std::string target_name;
    std::vector<ProfileSourceSummary> sources;
};

struct PublicRunStatus {
    std::string run_id;
    std::string state = "unavailable";
    std::string phase = "idle";
    std::string activity = "idle";
    bool can_cancel = false;
    std::string error_code;
    std::string source_name;
    std::string target_name;
    std::int64_t speed_bps = 0;
    std::int64_t eta_seconds = -1;
    int source_progress = -1;
    int overall_progress = -1;
    std::string progress_accuracy = "indeterminate";
};

struct SanitizedHistoryEntry {
    std::string state;
    std::string error_code;
    std::string source_name;
    std::string target_name;
    std::string finished_at;
    int overall_progress = -1;
};

struct SanitizedHistoryPage {
    std::vector<SanitizedHistoryEntry> entries;
};

struct TargetStatus {
    std::string profile_id;
    std::string target_name;
    std::string state;
    bool connected = false;
    bool unlocked = false;
    bool mounted = false;
    bool safe_to_remove = false;
};

struct OperationResult {
    std::string operation;
    std::string operation_id;
    std::string profile_id;
    std::string run_id;
    bool accepted = true;
};

} // namespace btrfsbackup::daemon
