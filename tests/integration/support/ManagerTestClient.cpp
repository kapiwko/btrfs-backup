// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerTestClient.hpp"

#include <core/ManagerProtocol.hpp>

#include <systemd/sd-bus.h>

#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace btrfsbackup::integration {

namespace {

constexpr std::size_t maximum_reply_bytes = 1024U * 1024U;

using BusMessage = std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)>;

[[nodiscard]] std::runtime_error bus_error(
    std::string_view method,
    const sd_bus_error& error,
    int result
) {
    const char* detail = error.message != nullptr ? error.message : std::strerror(-result);
    return std::runtime_error(std::string(method) + " failed: " + detail);
}

[[nodiscard]] std::string reply_payload(sd_bus_message* reply) {
    const char* payload = nullptr;
    if (sd_bus_message_read(reply, "s", &payload) < 0 || payload == nullptr)
        throw std::runtime_error("manager returned an invalid reply");
    const std::string result(payload);
    if (result.size() > maximum_reply_bytes)
        throw std::runtime_error("manager reply exceeds the integration client limit");
    return result;
}

} // namespace

struct ManagerTestClient::Implementation {
    std::unique_ptr<sd_bus, decltype(&sd_bus_unref)> bus{nullptr, sd_bus_unref};
};

ManagerTestClient::ManagerTestClient()
    : implementation_(std::make_unique<Implementation>()) {
    sd_bus* raw_bus = nullptr;
    const int result = sd_bus_open_system(&raw_bus);
    implementation_->bus.reset(raw_bus);
    if (result < 0)
        throw std::runtime_error(std::string("cannot connect to the system bus: ") + std::strerror(-result));
}

ManagerTestClient::~ManagerTestClient() noexcept = default;

std::string ManagerTestClient::call(std::string_view method) const {
    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    const std::string method_name(method);
    const int result = sd_bus_call_method(
        implementation_->bus.get(),
        manager_protocol::service_name,
        manager_protocol::object_path,
        manager_protocol::interface_name,
        method_name.c_str(),
        &error,
        &raw_reply,
        nullptr
    );
    BusMessage reply(raw_reply, sd_bus_message_unref);
    if (result < 0) {
        const auto failure = bus_error(method, error, result);
        sd_bus_error_free(&error);
        throw failure;
    }
    sd_bus_error_free(&error);
    return reply_payload(reply.get());
}

std::string ManagerTestClient::call(std::string_view method, std::string_view argument) const {
    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    const std::string method_name(method);
    const std::string value(argument);
    const int result = sd_bus_call_method(
        implementation_->bus.get(),
        manager_protocol::service_name,
        manager_protocol::object_path,
        manager_protocol::interface_name,
        method_name.c_str(),
        &error,
        &raw_reply,
        "s",
        value.c_str()
    );
    BusMessage reply(raw_reply, sd_bus_message_unref);
    if (result < 0) {
        const auto failure = bus_error(method, error, result);
        sd_bus_error_free(&error);
        throw failure;
    }
    sd_bus_error_free(&error);
    return reply_payload(reply.get());
}

std::string ManagerTestClient::call(
    std::string_view method,
    std::string_view first,
    std::string_view second,
    std::string_view third
) const {
    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    const std::string method_name(method);
    const std::string first_value(first);
    const std::string second_value(second);
    const std::string third_value(third);
    const int result = sd_bus_call_method(
        implementation_->bus.get(),
        manager_protocol::service_name,
        manager_protocol::object_path,
        manager_protocol::interface_name,
        method_name.c_str(),
        &error,
        &raw_reply,
        "sss",
        first_value.c_str(),
        second_value.c_str(),
        third_value.c_str()
    );
    BusMessage reply(raw_reply, sd_bus_message_unref);
    if (result < 0) {
        const auto failure = bus_error(method, error, result);
        sd_bus_error_free(&error);
        throw failure;
    }
    sd_bus_error_free(&error);
    return reply_payload(reply.get());
}

std::string ManagerTestClient::call_with_fd(
    std::string_view method,
    std::string_view argument,
    int descriptor
) const {
    sd_bus_message* raw_message = nullptr;
    const std::string method_name(method);
    int result = sd_bus_message_new_method_call(
        implementation_->bus.get(),
        &raw_message,
        manager_protocol::service_name,
        manager_protocol::object_path,
        manager_protocol::interface_name,
        method_name.c_str()
    );
    BusMessage message(raw_message, sd_bus_message_unref);
    if (result < 0)
        throw std::runtime_error("cannot construct descriptor-bearing method call");
    const std::string value(argument);
    result = sd_bus_message_append(message.get(), "sh", value.c_str(), descriptor);
    if (result < 0)
        throw std::runtime_error("cannot append descriptor-bearing method arguments");

    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    result = sd_bus_call(implementation_->bus.get(), message.get(), 0, &error, &raw_reply);
    BusMessage reply(raw_reply, sd_bus_message_unref);
    if (result < 0) {
        const auto failure = bus_error(method, error, result);
        sd_bus_error_free(&error);
        throw failure;
    }
    sd_bus_error_free(&error);
    return reply_payload(reply.get());
}

int ManagerTestClient::call_for_fd(std::string_view method, std::string_view argument) const {
    sd_bus_error error{};
    sd_bus_message* raw_reply = nullptr;
    const std::string method_name(method);
    const std::string value(argument);
    const int result = sd_bus_call_method(
        implementation_->bus.get(),
        manager_protocol::service_name,
        manager_protocol::object_path,
        manager_protocol::interface_name,
        method_name.c_str(),
        &error,
        &raw_reply,
        "s",
        value.c_str()
    );
    BusMessage reply(raw_reply, sd_bus_message_unref);
    if (result < 0) {
        const auto failure = bus_error(method, error, result);
        sd_bus_error_free(&error);
        throw failure;
    }
    sd_bus_error_free(&error);
    int descriptor = -1;
    if (sd_bus_message_read(reply.get(), "h", &descriptor) < 0 || descriptor < 0)
        throw std::runtime_error("manager returned an invalid file descriptor");
    const int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    if (duplicate < 0)
        throw std::runtime_error("cannot retain manager file descriptor");
    return duplicate;
}

} // namespace btrfsbackup::integration
