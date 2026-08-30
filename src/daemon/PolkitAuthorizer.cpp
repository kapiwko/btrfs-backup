// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/PolkitAuthorizer.hpp>

#include <systemd/sd-bus.h>

#include <cerrno>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require_bus_success(int result, const char* operation) {
    if (result < 0)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(-result));
}

void append_subject(sd_bus_message* message, const std::string& caller_bus_name) {
    require_bus_success(sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, "sa{sv}"), "cannot open polkit subject");
    require_bus_success(sd_bus_message_append(message, "s", "system-bus-name"), "cannot append polkit subject kind");
    require_bus_success(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}"), "cannot open polkit subject details");
    require_bus_success(sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv"), "cannot open polkit subject entry");
    require_bus_success(sd_bus_message_append(message, "s", "name"), "cannot append polkit subject key");
    require_bus_success(sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "s"), "cannot open polkit subject value");
    require_bus_success(sd_bus_message_append(message, "s", caller_bus_name.c_str()), "cannot append polkit caller");
    require_bus_success(sd_bus_message_close_container(message), "cannot close polkit subject value");
    require_bus_success(sd_bus_message_close_container(message), "cannot close polkit subject entry");
    require_bus_success(sd_bus_message_close_container(message), "cannot close polkit subject details");
    require_bus_success(sd_bus_message_close_container(message), "cannot close polkit subject");
}

} // namespace

namespace btrfsbackup::daemon {

PolkitAuthorizer::PolkitAuthorizer(sd_bus* bus) : bus_(bus) {
    if (bus_ == nullptr)
        throw std::invalid_argument("polkit authorizer requires a D-Bus connection");
}

bool PolkitAuthorizer::authorize(
    const std::string& caller_bus_name,
    control::ManagerAuthorizationAction action
) {
    sd_bus_message* raw_request = nullptr;
    require_bus_success(
        sd_bus_message_new_method_call(
            bus_,
            &raw_request,
            "org.freedesktop.PolicyKit1",
            "/org/freedesktop/PolicyKit1/Authority",
            "org.freedesktop.PolicyKit1.Authority",
            "CheckAuthorization"
        ),
        "cannot create polkit request"
    );
    std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> request(raw_request, sd_bus_message_unref);
    append_subject(request.get(), caller_bus_name);
    require_bus_success(
        sd_bus_message_append(request.get(), "s", control::manager_authorization_action_id(action)),
        "cannot append polkit action"
    );
    require_bus_success(sd_bus_message_open_container(request.get(), SD_BUS_TYPE_ARRAY, "{ss}"), "cannot open polkit details");
    require_bus_success(sd_bus_message_close_container(request.get()), "cannot close polkit details");
    require_bus_success(sd_bus_message_append(request.get(), "us", 1U, ""), "cannot append polkit flags");

    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    const int result = sd_bus_call(bus_, request.get(), 0, &error, &raw_reply);
    std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> reply(raw_reply, sd_bus_message_unref);
    if (result < 0) {
        const std::string message = error.message == nullptr ? "polkit authorization failed" : error.message;
        sd_bus_error_free(&error);
        throw std::runtime_error(message);
    }
    sd_bus_error_free(&error);
    require_bus_success(sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_STRUCT, "bba{ss}"), "cannot read polkit reply");
    int authorized = 0;
    int challenge = 0;
    require_bus_success(sd_bus_message_read(reply.get(), "bb", &authorized, &challenge), "cannot read polkit decision");
    return authorized != 0;
}

bool PolkitAuthorizer::caller_is_active(const std::string& caller_bus_name) {
    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    const int result = sd_bus_call_method(
        bus_,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameHasOwner",
        &error,
        &raw_reply,
        "s",
        caller_bus_name.c_str()
    );
    std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> reply(raw_reply, sd_bus_message_unref);
    sd_bus_error_free(&error);
    if (result < 0)
        return false;
    int has_owner = 0;
    return sd_bus_message_read(reply.get(), "b", &has_owner) >= 0 && has_owner != 0;
}

} // namespace btrfsbackup::daemon
