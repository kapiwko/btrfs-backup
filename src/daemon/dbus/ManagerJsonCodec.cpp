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
            {"enabled", profile.enabled},
            {"targetName", profile.target_name},
            {"sources", std::move(sources)},
            {"configurationValid", profile.configuration_valid},
            {"configurationErrorCode", profile.configuration_error_code},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const PublicStatusResponse& status) const {
    config::json::Json result = config::json::Json::parse(
        state::document::RunStatusDocumentCodec{}.serialize_public(status.run)
    );
    result["schemaVersion"] = manager_protocol::public_status_schema_version;
    result["sourceIndex"] = status.source_index;
    result["sourceCount"] = status.source_count;
    result["startedAt"] = status.started_at;
    result["updatedAt"] = status.updated_at;
    result["lastSuccessAt"] = status.last_success_at;
    result["lastAttemptAt"] = status.last_attempt_at;
    result["lastAttemptState"] = status.last_attempt_state;
    return config::json::dump_json(result);
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
            {"startedAt", entry.started_at},
            {"finishedAt", entry.finished_at},
            {"sourceCount", entry.source_count},
            {"overallProgress", entry.overall_progress},
            {"bytesTransferred", entry.bytes_transferred},
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

std::string ManagerJsonCodec::encode(const BrowseSessionInfo& session) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::browse_session_schema_version},
        {"sessionId", session.session_id},
        {"profileId", session.profile_id},
        {"rootPath", session.root_path},
        {"expiresAt", session.expires_at},
        {"readOnly", session.read_only},
    });
}

std::string ManagerJsonCodec::encode(const std::vector<BackupCoverage>& coverage) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& item : coverage) {
        result.push_back({
            {"profileId", item.profile_id},
            {"sourceId", item.source_id},
            {"relativePath", item.relative_path},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const control::ProfileDetails& profile) const {
    config::json::Json document = config::json::Json::parse(profile.document);
    document.erase("hooks");
    if (document.contains("target") && document["target"].is_object()) {
        auto& target = document["target"];
        if (target.contains("activation") && target["activation"].is_object())
            target["activation"].erase("keyFile");
    }
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::profile_details_schema_version},
        {"profileId", profile.profile_id},
        {"generation", profile.generation},
        {"fingerprint", profile.fingerprint},
        {"document", std::move(document)},
        {"configurationValid", profile.configuration_valid},
        {"configurationErrorCode", profile.configuration_error_code},
        {"sourceCandidates", profile.source_candidates},
    });
}

std::string ManagerJsonCodec::encode(const std::vector<control::TargetCredential>& credentials) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& credential : credentials) {
        result.push_back({
            {"schemaVersion", manager_protocol::target_credentials_schema_version},
            {"id", credential.id},
            {"label", credential.label},
            {"type", credential.type},
            {"keyslot", credential.keyslot},
            {"managed", credential.managed},
            {"automatic", credential.automatic},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const std::vector<control::ProvisioningDevice>& devices) const {
    config::json::Json result = config::json::Json::array();
    for (const auto& device : devices) {
        result.push_back({
            {"schemaVersion", manager_protocol::device_provisioning_schema_version},
            {"candidateId", device.candidate_id},
            {"path", device.path},
            {"model", device.model},
            {"serial", device.serial},
            {"transport", device.transport},
            {"sizeBytes", device.size_bytes},
            {"removable", device.removable},
            {"mounted", device.mounted},
            {"containsData", device.contains_data},
        });
    }
    return config::json::dump_json(result);
}

std::string ManagerJsonCodec::encode(const control::DevicePreparationStatus& status) const {
    return config::json::dump_json({
        {"schemaVersion", manager_protocol::device_provisioning_schema_version},
        {"operationId", status.operation_id},
        {"profileId", status.profile_id},
        {"state", status.state},
        {"phase", status.phase},
        {"errorCode", status.error_code},
        {"canCancel", status.can_cancel},
    });
}

} // namespace btrfsbackup::daemon::dbus
