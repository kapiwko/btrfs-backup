// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerJsonCodec.hpp>

#include <utility>

#include <config/json/JsonIo.hpp>
#include <core/ManagerProtocol.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

namespace btrfsbackup::daemon::dbus {

std::string ManagerJsonCodec::encode(const ManagerCapabilities& capabilities) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::capabilities_schema_version},
        {"interface", capabilities.interface_name},
        {"apiMajor", capabilities.api_major},
        {"apiMinor", capabilities.api_minor},
        {"profileSchemaVersion", capabilities.profile_schema_version},
        {"publicStatusSchemaVersion", capabilities.public_status_schema_version},
        {"historySchemaVersion", capabilities.history_schema_version},
        {"deviceStateSchemaVersion", capabilities.device_state_schema_version},
        {"readOnly", capabilities.read_only},
        {"features", capabilities.features},
    });
}

std::string ManagerJsonCodec::encode(const std::vector<ProfileSummary>& profiles) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& profile : profiles) {
        config::json::Json sources = config::json::Json::array();
        for (const auto& source : profile.sources)
            sources.push_back({{"id", source.id}, {"name", source.name}});
        result.push_back({
            {"schemaVersion", manager_protocol::profile_summary_schema_version},
            {"profileId", profile.profile_id},
            {"name", profile.name},
            {"targetName", profile.target_name},
            {"sources", std::move(sources)},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const PublicRunStatus& status) const {
    return state::document::RunStatusDocumentCodec{}.serialize_public(status);
}

std::string ManagerJsonCodec::encode(const SanitizedHistoryPage& page) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& entry : page.entries) {
        result.push_back({
            {"schemaVersion", manager_protocol::history_schema_version},
            {"state", entry.state},
            {"errorCode", entry.error_code},
            {"sourceName", entry.source_name},
            {"targetName", entry.target_name},
            {"finishedAt", entry.finished_at},
            {"overallProgress", entry.overall_progress},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const TargetStatus& status) const {
    return state::document::TargetStatusDocumentCodec{}.serialize(status);
}

std::string ManagerJsonCodec::encode(const OperationResult& result) const {
    config::json::Json document{
        {"schemaVersion", manager_protocol::operation_result_schema_version},
        {"operation", result.operation},
        {"operationId", result.operation_id},
        {"profileId", result.profile_id},
        {"accepted", result.accepted},
    };
    if (!result.run_id.empty())
        document["runId"] = result.run_id;
    return config::json::dump_json(document);
}

} // namespace btrfsbackup::daemon::dbus
