// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <systemd/sd-bus.h>

#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s PROFILE HOLD_FILE\n", argv[0]);
        return 2;
    }

    sd_bus* bus = NULL;
    sd_bus_message* reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    const char* payload = NULL;
    int result = sd_bus_open_system(&bus);
    if (result < 0)
        goto fail;
    result = sd_bus_call_method(
        bus,
        "io.github.btrfsbackup.Manager1",
        "/io/github/btrfsbackup/Manager1",
        "io.github.btrfsbackup.Manager1",
        "OpenBrowseSession",
        &error,
        &reply,
        "s",
        argv[1]
    );
    if (result < 0)
        goto fail;
    result = sd_bus_message_read(reply, "s", &payload);
    if (result < 0)
        goto fail;

    puts(payload);
    fflush(stdout);
    while (access(argv[2], F_OK) == 0)
        usleep(10000);

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;

fail:
    fprintf(stderr, "browse session D-Bus call failed: %s\n", error.message != NULL ? error.message : "unknown error");
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 1;
}
