// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerProvisioningMethods.hpp>

#include <daemon/control/DeviceProvisioningService.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <core/ManagerProtocol.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerProvisioningMethods::ManagerProvisioningMethods(
    control::DeviceProvisioningService& device_provisioning,
    ManagerMethodSupport& support
)
    : device_provisioning_(device_provisioning),
      support_(support) {
}

int ManagerProvisioningMethods::inspect_storage_topology(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            return support_.reply_operational_json(message, error, "inspect-storage-topology", "", [&] {
                return support_.codec().encode(device_provisioning_.inspect_storage_topology(
                    ManagerMethodSupport::caller_bus_name(message)
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProvisioningMethods::inspect_existing_target(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* request_json = nullptr;
            int credential_fd = -1;
            const int read_result = sd_bus_message_read(message, "sh", &request_json, &credential_fd);
            if (read_result < 0)
                return read_result;
            const auto request = config::json::Json::parse(request_json == nullptr ? "{}" : request_json);
            return support_.reply_operational_json(message, error, "inspect-existing-target", "", [&] {
                return support_.codec().encode(device_provisioning_.inspect_existing_target(
                    ManagerMethodSupport::caller_bus_name(message),
                    request.value("topologyGeneration", ""),
                    request.value("candidateId", ""),
                    credential_fd
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProvisioningMethods::build_device_preparation_plan(
    sd_bus_message* message,
    sd_bus_error* error
) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* request_json = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &request_json);
            if (read_result < 0)
                return read_result;
            const auto request = config::json::Json::parse(request_json == nullptr ? "{}" : request_json);
            const std::string mode = request.value("mode", "");
            const auto provisioning_mode = provisioning::provisioning_mode_from_name(mode);
            if (!provisioning_mode.has_value())
                throw ValidationError("provisioning mode is not implemented");
            return support_.reply_operational_json(message, error, "build-device-preparation-plan", "", [&] {
                return support_.codec().encode(device_provisioning_.build_device_preparation_plan(
                    ManagerMethodSupport::caller_bus_name(message),
                    request.value("topologyGeneration", ""),
                    request.value("candidateId", ""),
                    *provisioning_mode,
                    request.value("inspectionId", "")
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProvisioningMethods::list_source_candidates(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            return support_.reply_operational_json(message, error, "list-source-candidates", "", [&] {
                const auto candidates = device_provisioning_.list_source_candidates(
                    ManagerMethodSupport::caller_bus_name(message)
                );
                auto result = config::json::Json::array();
                for (const auto& candidate : candidates) {
                    result.push_back({
                        {"id", candidate.id},
                        {"path", candidate.path},
                        {"filesystemUuid", candidate.filesystem_uuid},
                        {"mountRoot", candidate.mount_root},
                        {"localSnapshotRoot", candidate.local_snapshot_root},
                    });
                }
                return config::json::dump_json(result);
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProvisioningMethods::start_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* request_json = nullptr;
            int passphrase_fd = -1;
            const int read_result = sd_bus_message_read(message, "sh", &request_json, &passphrase_fd);
            if (read_result < 0)
                return read_result;
            const auto request = config::json::Json::parse(request_json == nullptr ? "{}" : request_json);
            control::DevicePreparationRequest parsed{
                .profile_id = request.value("profileId", ""),
                .profile_name = request.value("profileName", ""),
                .plan_id = request.value("planId", ""),
                .source_candidate_id = request.value("sourceCandidateId", ""),
                .sources = {},
                .passphrase_label = request.value("passphraseLabel", ""),
                .create_automatic_key = request.value("createAutomaticKey", true),
            };
            if (request.contains("sources")) {
                for (const auto& source : request.at("sources")) {
                    parsed.sources.push_back({
                        .candidate_id = source.value("candidateId", ""),
                        .name = source.value("name", ""),
                        .subvolume = {},
                        .filesystem_uuid = {},
                        .mount_root = {},
                        .local_snapshot_dir = {},
                        .local_retention = source.value("localRetention", std::size_t{30}),
                        .remote_retention = source.value("remoteRetention", std::size_t{30}),
                    });
                }
            }
            const std::string profile = parsed.profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::device_provisioning,
                profile,
                [&] {
                    return support_.codec().encode(device_provisioning_.start(
                        ManagerMethodSupport::caller_bus_name(message),
                        ManagerMethodSupport::caller_uid(message),
                        parsed,
                        passphrase_fd
                    ));
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProvisioningMethods::get_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* operation_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &operation_id);
            if (read_result < 0)
                return read_result;
            const std::string operation = operation_id == nullptr ? "" : operation_id;
            return support_.reply_operational_json(message, error, "get-device-preparation", "", [&] {
                return support_.codec().encode(device_provisioning_.status(
                    ManagerMethodSupport::caller_bus_name(message),
                    ManagerMethodSupport::caller_uid(message),
                    operation
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProvisioningMethods::cancel_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* operation_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &operation_id);
            if (read_result < 0)
                return read_result;
            const std::string operation = operation_id == nullptr ? "" : operation_id;
            return support_.reply_operational_json(message, error, "cancel-device-preparation", "", [&] {
                device_provisioning_.cancel(
                    ManagerMethodSupport::caller_bus_name(message),
                    ManagerMethodSupport::caller_uid(message),
                    operation
                );
                return config::json::dump_json({
                    {"schemaVersion", manager_protocol::operation_result_schema_version},
                    {"operation", "cancel-device-preparation"},
                    {"operationId", operation},
                    {"profileId", ""},
                    {"accepted", true},
                });
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
