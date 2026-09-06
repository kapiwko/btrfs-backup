// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerBrowseMethods.hpp>

#include <daemon/ManagerService.hpp>
#include <daemon/control/BrowseSessionService.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>

#include <config/json/JsonIo.hpp>
#include <core/Identifiers.hpp>
#include <core/ManagerProtocol.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerBrowseMethods::ManagerBrowseMethods(
    ManagerService& service,
    control::BrowseSessionService& browse_sessions,
    ManagerMethodSupport& support
)
    : service_(service),
      browse_sessions_(browse_sessions),
      support_(support) {
}

int ManagerBrowseMethods::open_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::browse_backups,
                profile,
                [&] {
                    return support_.codec().encode(browse_sessions_.open(
                        ManagerMethodSupport::caller_bus_name(message),
                        ManagerMethodSupport::caller_access_identity(message),
                        profile
                    ));
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::renew_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &session_id);
            if (read_result < 0)
                return read_result;
            return ManagerMethodSupport::reply_json(
                message,
                support_.codec().encode(browse_sessions_.renew(
                    ManagerMethodSupport::caller_bus_name(message),
                    session_id == nullptr ? "" : session_id
                ))
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::begin_browse_operation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &session_id);
            if (read_result < 0)
                return read_result;
            const std::string lease_id = browse_sessions_.begin_operation(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id
            );
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", manager_protocol::operation_result_schema_version},
                                                                 {"leaseId", lease_id},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::end_browse_operation(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* lease_id = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &session_id, &lease_id);
            if (read_result < 0)
                return read_result;
            browse_sessions_.end_operation(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id,
                lease_id == nullptr ? "" : lease_id
            );
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", manager_protocol::operation_result_schema_version},
                                                                 {"operation", "end-browse-operation"},
                                                                 {"accepted", true},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::close_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &session_id);
            if (read_result < 0)
                return read_result;
            browse_sessions_.close(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id
            );
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", manager_protocol::operation_result_schema_version},
                                                                 {"operation", "close-browse-session"},
                                                                 {"accepted", true},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::list_browse_directory(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* relative_path = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &session_id, &relative_path);
            if (read_result < 0)
                return read_result;
            config::json::Json entries = config::json::Json::array();
            for (const auto& entry : browse_sessions_.list_directory(
                     ManagerMethodSupport::caller_bus_name(message),
                     session_id == nullptr ? "" : session_id,
                     relative_path == nullptr ? "" : relative_path
                 )) {
                entries.push_back({
                    {"name", entry.name},
                    {"kind", entry.directory ? "directory" : "file"},
                    {"size", entry.size},
                    {"mode", entry.mode},
                    {"modifiedAt", entry.modified_at},
                });
            }
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", 1},
                                                                 {"entries", std::move(entries)},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::list_browse_directory_page(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* relative_path = nullptr;
            const char* continuation_token = nullptr;
            std::uint32_t limit = 0;
            const int read_result = sd_bus_message_read(
                message,
                "sssu",
                &session_id,
                &relative_path,
                &continuation_token,
                &limit
            );
            if (read_result < 0)
                return read_result;
            auto page = browse_sessions_.list_directory_page(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id,
                relative_path == nullptr ? "" : relative_path,
                continuation_token == nullptr ? "" : continuation_token,
                limit
            );
            config::json::Json entries = config::json::Json::array();
            for (const auto& entry : page.entries) {
                entries.push_back({
                    {"name", entry.name},
                    {"kind", entry.directory ? "directory" : "file"},
                    {"size", entry.size},
                    {"mode", entry.mode},
                    {"modifiedAt", entry.modified_at},
                });
            }
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", 1},
                                                                 {"entries", std::move(entries)},
                                                                 {"continuationToken", page.continuation_token},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::list_previous_versions(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* profile_id = nullptr;
            const char* source_id = nullptr;
            const char* relative_path = nullptr;
            const char* continuation_token = nullptr;
            std::uint32_t limit = 0;
            const int read_result = sd_bus_message_read(
                message,
                "sssssu",
                &session_id,
                &profile_id,
                &source_id,
                &relative_path,
                &continuation_token,
                &limit
            );
            if (read_result < 0)
                return read_result;
            auto page = browse_sessions_.list_previous_versions(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id,
                profile_id == nullptr ? "" : profile_id,
                source_id == nullptr ? "" : source_id,
                relative_path == nullptr ? "" : relative_path,
                continuation_token == nullptr ? "" : continuation_token,
                limit
            );
            config::json::Json entries = config::json::Json::array();
            for (const auto& version : page.entries) {
                entries.push_back({
                    {"snapshotId", version.snapshot_id},
                    {"createdAt", version.created_at},
                    {"kind", version.entry.directory ? "directory" : "file"},
                    {"size", version.entry.size},
                    {"mode", version.entry.mode},
                    {"modifiedAt", version.entry.modified_at},
                });
            }
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", manager_protocol::previous_versions_page_schema_version},
                                                                 {"entries", std::move(entries)},
                                                                 {"continuationToken", page.continuation_token},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::inspect_browse_entry(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* relative_path = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &session_id, &relative_path);
            if (read_result < 0)
                return read_result;
            const auto entry = browse_sessions_.inspect_entry(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id,
                relative_path == nullptr ? "" : relative_path
            );
            return ManagerMethodSupport::reply_json(message, config::json::dump_json({
                                                                 {"schemaVersion", 1},
                                                                 {"name", entry.name},
                                                                 {"kind", entry.directory ? "directory" : "file"},
                                                                 {"size", entry.size},
                                                                 {"mode", entry.mode},
                                                                 {"modifiedAt", entry.modified_at},
                                                             }));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::open_browse_file(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* relative_path = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &session_id, &relative_path);
            if (read_result < 0)
                return read_result;
            auto file = browse_sessions_.open_file(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id,
                relative_path == nullptr ? "" : relative_path
            );
            return sd_bus_reply_method_return(message, "h", file.get());
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::open_browse_entry(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const char* relative_path = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &session_id, &relative_path);
            if (read_result < 0)
                return read_result;
            auto entry = browse_sessions_.open_entry(
                ManagerMethodSupport::caller_bus_name(message),
                session_id == nullptr ? "" : session_id,
                relative_path == nullptr ? "" : relative_path
            );
            return sd_bus_reply_method_return(message, "h", entry.get());
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::inspect_browse_repository(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* session_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &session_id);
            if (read_result < 0)
                return read_result;
            return ManagerMethodSupport::reply_json(
                message,
                browse_sessions_.inspect_repository(
                    ManagerMethodSupport::caller_bus_name(message),
                    session_id == nullptr ? "" : session_id
                )
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerBrowseMethods::resolve_backup_coverage(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* local_path = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &local_path);
            if (read_result < 0)
                return read_result;
            std::vector<ProfileId> profile_ids;
            for (const auto& profile : service_.list_profiles())
                profile_ids.emplace_back(profile.profile_id);
            return support_.reply_operational_json(message, error, "resolve-backup-coverage", "", [&] {
                return support_.codec().encode(browse_sessions_.resolve_coverage(
                    ManagerMethodSupport::caller_bus_name(message),
                    local_path == nullptr ? "" : local_path,
                    profile_ids
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
