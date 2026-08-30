// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <core/ManagerProtocol.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

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

using PublicRunStatus = btrfsbackup::state::document::PublicRunStatusV3;

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
    struct Storage {
        std::uint64_t capacity_bytes = 0;
        std::uint64_t used_bytes = 0;
        std::uint64_t available_bytes = 0;
        int usage_percent = 0;
        std::string measured_at;
        bool live = false;
        std::string space_state;
    };
    std::optional<Storage> storage;
};

struct OperationResult {
    std::string operation;
    std::string operation_id;
    std::string profile_id;
    std::string run_id;
    bool accepted = true;
};

} // namespace btrfsbackup::daemon
