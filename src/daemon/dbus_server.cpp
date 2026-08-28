// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus_server.hpp>

#include <systemd/sd-bus.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <config/model/json_io.hpp>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int) {
    stop_requested = 1;
}

btrfsbackup::config::Json response_json(const btrfsbackup::daemon::ManagerCapabilities& capabilities) {
    return {
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
    };
}

btrfsbackup::config::Json response_json(const std::vector<btrfsbackup::daemon::ProfileSummary>& profiles) {
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
    for (const auto& profile : profiles) {
        btrfsbackup::config::Json sources = btrfsbackup::config::Json::array();
        for (const auto& source : profile.sources) {
            sources.push_back({{"id", source.id}, {"name", source.name}});
        }
        result.push_back({
            {"schemaVersion", 1},
            {"profileId", profile.profile_id},
            {"name", profile.name},
            {"targetName", profile.target_name},
            {"sources", std::move(sources)},
        });
    }
    return result;
}

btrfsbackup::config::Json response_json(const btrfsbackup::daemon::PublicRunStatus& status) {
    return {
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
    };
}

btrfsbackup::config::Json response_json(const btrfsbackup::daemon::SanitizedHistoryPage& page) {
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
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
    return result;
}

btrfsbackup::config::Json response_json(const btrfsbackup::daemon::TargetStatus& status) {
    return {
        {"schemaVersion", 1},
        {"profileId", status.profile_id},
        {"targetName", status.target_name},
        {"state", status.state},
        {"connected", status.connected},
        {"unlocked", status.unlocked},
        {"mounted", status.mounted},
        {"safeToRemove", status.safe_to_remove},
    };
}

int reply_json(sd_bus_message* message, sd_bus_error* error, const std::function<btrfsbackup::config::Json()>& operation) {
    try {
        const std::string payload = btrfsbackup::config::dump_json(operation());
        return sd_bus_reply_method_return(message, "s", payload.c_str());
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: request failed: " << exception.what() << '\n';
        return sd_bus_error_set_const(
            error,
            SD_BUS_ERROR_INVALID_ARGS,
            "manager request is invalid or data is unavailable"
        );
    }
}

int get_capabilities(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto& service = *static_cast<btrfsbackup::daemon::ManagerService*>(userdata);
    return reply_json(message, error, [&] { return response_json(service.get_capabilities()); });
}

int list_profiles(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto& service = *static_cast<btrfsbackup::daemon::ManagerService*>(userdata);
    return reply_json(message, error, [&] { return response_json(service.list_profiles()); });
}

int get_status(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& service = *static_cast<btrfsbackup::daemon::ManagerService*>(userdata);
    return reply_json(message, error, [&] {
        return response_json(service.get_status(profile_id == nullptr ? "" : profile_id));
    });
}

int get_history_sanitized(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    std::uint32_t offset = 0;
    std::uint32_t limit = 0;
    const int read_result = sd_bus_message_read(message, "suu", &profile_id, &offset, &limit);
    if (read_result < 0)
        return read_result;
    auto& service = *static_cast<btrfsbackup::daemon::ManagerService*>(userdata);
    return reply_json(message, error, [&] {
        return response_json(service.get_history_sanitized(profile_id == nullptr ? "" : profile_id, offset, limit));
    });
}

int get_device_state(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    const char* profile_id = nullptr;
    const int read_result = sd_bus_message_read(message, "s", &profile_id);
    if (read_result < 0)
        return read_result;
    auto& service = *static_cast<btrfsbackup::daemon::ManagerService*>(userdata);
    return reply_json(message, error, [&] {
        return response_json(service.get_device_state(profile_id == nullptr ? "" : profile_id));
    });
}

const sd_bus_vtable manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCapabilities", "", "s", get_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ListProfiles", "", "s", list_profiles, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStatus", "s", "s", get_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetHistorySanitized", "suu", "s", get_history_sanitized, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetDeviceState", "s", "s", get_device_state, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

void require_success(int result, const char* operation) {
    if (result < 0)
        throw std::runtime_error(std::string(operation) + ": " + std::strerror(-result));
}

} // namespace

namespace btrfsbackup::daemon {

int run_dbus_server(ManagerService& service, const std::string& bus_address) {
    std::unique_ptr<sd_bus, decltype(&sd_bus_unref)> bus(nullptr, sd_bus_unref);
    sd_bus* raw_bus = nullptr;
    if (bus_address.empty()) {
        require_success(sd_bus_default_system(&raw_bus), "cannot connect to the system bus");
    } else {
        require_success(sd_bus_new(&raw_bus), "cannot allocate a D-Bus connection");
        bus.reset(raw_bus);
        require_success(sd_bus_set_bus_client(bus.get(), 1), "cannot configure the D-Bus client");
        require_success(sd_bus_set_address(bus.get(), bus_address.c_str()), "cannot set the D-Bus address");
        require_success(sd_bus_start(bus.get()), "cannot connect to the requested D-Bus address");
        raw_bus = nullptr;
    }
    if (!bus)
        bus.reset(raw_bus);

    std::unique_ptr<sd_bus_slot, decltype(&sd_bus_slot_unref)> slot(nullptr, sd_bus_slot_unref);
    sd_bus_slot* raw_slot = nullptr;
    require_success(
        sd_bus_add_object_vtable(
            bus.get(),
            &raw_slot,
            manager_object_path,
            manager_interface,
            manager_vtable,
            &service
        ),
        "cannot export the manager object"
    );
    slot.reset(raw_slot);
    require_success(sd_bus_request_name(bus.get(), manager_bus_name, 0), "cannot acquire the manager bus name");

    stop_requested = 0;
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    while (!stop_requested) {
        const int processed = sd_bus_process(bus.get(), nullptr);
        if (processed < 0 && processed != -EINTR)
            require_success(processed, "D-Bus processing failed");
        if (processed > 0)
            continue;
        const int waited = sd_bus_wait(bus.get(), 1000000);
        if (waited < 0 && waited != -EINTR)
            require_success(waited, "D-Bus wait failed");
    }
    return 0;
}

} // namespace btrfsbackup::daemon
