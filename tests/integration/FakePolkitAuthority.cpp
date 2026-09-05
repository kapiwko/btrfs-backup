// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <systemd/sd-bus.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

struct AuthorityContext {
    std::string log_path;
    std::chrono::milliseconds delay;
};

void request_stop(int) {
    stop_requested = 1;
}

void require_success(int result, const char* operation) {
    if (result < 0)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(-result));
}

std::string read_subject_name(sd_bus_message* message) {
    require_success(sd_bus_message_enter_container(message, SD_BUS_TYPE_STRUCT, "sa{sv}"), "enter subject");
    const char* subject_kind = nullptr;
    require_success(sd_bus_message_read(message, "s", &subject_kind), "read subject kind");
    require_success(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}"), "enter subject details");
    std::string subject_name;
    while (sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        require_success(sd_bus_message_read(message, "s", &key), "read subject key");
        require_success(sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "s"), "enter subject value");
        const char* value = nullptr;
        require_success(sd_bus_message_read(message, "s", &value), "read subject value");
        if (key != nullptr && std::string(key) == "name" && value != nullptr)
            subject_name = value;
        require_success(sd_bus_message_exit_container(message), "exit subject value");
        require_success(sd_bus_message_exit_container(message), "exit subject entry");
    }
    require_success(sd_bus_message_exit_container(message), "exit subject details");
    require_success(sd_bus_message_exit_container(message), "exit subject");
    return subject_name;
}

int check_authorization(sd_bus_message* message, void* userdata, sd_bus_error* ret_error) {
    auto& context = *static_cast<AuthorityContext*>(userdata);
    try {
        const std::string subject_name = read_subject_name(message);
        const char* action = nullptr;
        require_success(sd_bus_message_read(message, "s", &action), "read action");
        std::ofstream log(context.log_path, std::ios::app);
        log << subject_name << ' ' << (action == nullptr ? "" : action) << '\n';
        log.close();
        if (std::filesystem::exists(context.log_path + ".delay"))
            std::this_thread::sleep_for(context.delay);

        sd_bus_message* raw_reply = nullptr;
        require_success(sd_bus_message_new_method_return(message, &raw_reply), "create reply");
        std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> reply(raw_reply, sd_bus_message_unref);
        require_success(sd_bus_message_open_container(reply.get(), SD_BUS_TYPE_STRUCT, "bba{ss}"), "open decision");
        const int authorized = std::filesystem::exists(context.log_path + ".allow") ? 1 : 0;
        require_success(sd_bus_message_append(reply.get(), "bb", authorized, 0), "append decision");
        require_success(sd_bus_message_open_container(reply.get(), SD_BUS_TYPE_ARRAY, "{ss}"), "open details");
        require_success(sd_bus_message_close_container(reply.get()), "close details");
        require_success(sd_bus_message_close_container(reply.get()), "close decision");
        return sd_bus_send(sd_bus_message_get_bus(message), reply.get(), nullptr);
    } catch (const std::exception& error) {
        return sd_bus_error_setf(
            ret_error,
            "org.freedesktop.PolicyKit1.Error.Failed",
            "%s",
            error.what()
        );
    }
}

const sd_bus_vtable authority_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CheckAuthorization", "(sa{sv})sa{ss}us", "(bba{ss})", check_authorization, 0),
    SD_BUS_VTABLE_END,
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    try {
        AuthorityContext context{argv[2], std::chrono::milliseconds(std::stoll(argv[3]))};
        sd_bus* raw_bus = nullptr;
        require_success(sd_bus_new(&raw_bus), "create bus");
        std::unique_ptr<sd_bus, decltype(&sd_bus_unref)> bus(raw_bus, sd_bus_unref);
        require_success(sd_bus_set_bus_client(bus.get(), 1), "configure bus");
        require_success(sd_bus_set_address(bus.get(), argv[1]), "set address");
        require_success(sd_bus_start(bus.get()), "start bus");
        sd_bus_slot* raw_slot = nullptr;
        require_success(
            sd_bus_add_object_vtable(
                bus.get(),
                &raw_slot,
                "/org/freedesktop/PolicyKit1/Authority",
                "org.freedesktop.PolicyKit1.Authority",
                authority_vtable,
                &context
            ),
            "export authority"
        );
        std::unique_ptr<sd_bus_slot, decltype(&sd_bus_slot_unref)> slot(raw_slot, sd_bus_slot_unref);
        require_success(sd_bus_request_name(bus.get(), "org.freedesktop.PolicyKit1", 0), "own authority name");
        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        while (!stop_requested) {
            require_success(sd_bus_process(bus.get(), nullptr), "process bus");
            require_success(sd_bus_wait(bus.get(), 100000), "wait for bus");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fake-polkit-authority: " << error.what() << '\n';
        return 1;
    }
}
