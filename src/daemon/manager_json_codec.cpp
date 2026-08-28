// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/manager_json_codec.hpp>

#include <utility>

#include <config/model/json_io.hpp>

namespace btrfsbackup::daemon {

std::string ManagerJsonCodec::encode(const ManagerCapabilities& capabilities) const {
    return config::dump_json({
        {"schemaVersion", 1},
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
    config::Json result = config::Json::array();
    for (const auto& profile : profiles) {
        config::Json sources = config::Json::array();
        for (const auto& source : profile.sources)
            sources.push_back({{"id", source.id}, {"name", source.name}});
        result.push_back({
            {"schemaVersion", 1},
            {"profileId", profile.profile_id},
            {"name", profile.name},
            {"targetName", profile.target_name},
            {"sources", std::move(sources)},
        });
    }
    return config::dump_json(result);
}

std::string ManagerJsonCodec::encode(const PublicRunStatus& status) const {
    return config::dump_json({
        {"schemaVersion", 3},
        {"state", status.state},
        {"errorCode", status.error_code},
        {"sourceName", status.source_name},
        {"targetName", status.target_name},
        {"speedBps", status.speed_bps},
        {"etaSeconds", status.eta_seconds},
        {"sourceProgress", status.source_progress},
        {"overallProgress", status.overall_progress},
        {"progressAccuracy", status.progress_accuracy},
    });
}

std::string ManagerJsonCodec::encode(const SanitizedHistoryPage& page) const {
    config::Json result = config::Json::array();
    for (const auto& entry : page.entries) {
        result.push_back({
            {"schemaVersion", 1},
            {"state", entry.state},
            {"errorCode", entry.error_code},
            {"sourceName", entry.source_name},
            {"targetName", entry.target_name},
            {"finishedAt", entry.finished_at},
            {"overallProgress", entry.overall_progress},
        });
    }
    return config::dump_json(result);
}

std::string ManagerJsonCodec::encode(const TargetStatus& status) const {
    return config::dump_json({
        {"schemaVersion", 1},
        {"profileId", status.profile_id},
        {"targetName", status.target_name},
        {"state", status.state},
        {"connected", status.connected},
        {"unlocked", status.unlocked},
        {"mounted", status.mounted},
        {"safeToRemove", status.safe_to_remove},
    });
}

std::string ManagerJsonCodec::encode(const OperationResult& result) const {
    config::Json document{
        {"schemaVersion", 1},
        {"operation", result.operation},
        {"profileId", result.profile_id},
        {"accepted", result.accepted},
    };
    if (!result.run_id.empty())
        document["runId"] = result.run_id;
    return config::dump_json(document);
}

} // namespace btrfsbackup::daemon
