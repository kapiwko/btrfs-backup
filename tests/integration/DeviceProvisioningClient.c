// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE

#include <systemd/sd-bus.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* service = "io.github.btrfsbackup.Manager1";
static const char* object = "/io/github/btrfsbackup/Manager1";
static const char* interface = "io.github.btrfsbackup.Manager1";

static void die(const char* message) {
    fprintf(stderr, "device provisioning client: %s\n", message);
    exit(EXIT_FAILURE);
}

static char* reply_payload(sd_bus_message* reply) {
    const char* payload = NULL;
    if (sd_bus_message_read(reply, "s", &payload) < 0 || payload == NULL)
        die("manager returned an invalid reply");
    char* result = strdup(payload);
    if (result == NULL)
        die("cannot allocate reply buffer");
    return result;
}

static char* call(sd_bus* bus, const char* method, const char* signature, const char* argument) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;
    int result = signature == NULL
        ? sd_bus_call_method(bus, service, object, interface, method, &error, &reply, NULL)
        : sd_bus_call_method(bus, service, object, interface, method, &error, &reply, signature, argument);
    if (result < 0) {
        fprintf(
            stderr,
            "device provisioning client: %s failed: %s\n",
            method,
            error.message != NULL ? error.message : strerror(-result)
        );
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        exit(EXIT_FAILURE);
    }
    char* payload = reply_payload(reply);
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return payload;
}

static char* json_string_between(const char* begin, const char* end, const char* key) {
    char pattern[128];
    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern))
        die("JSON key is too long");
    const char* position = strstr(begin, pattern);
    if (position == NULL || (end != NULL && position >= end))
        return NULL;
    position += strlen(pattern);
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')
        ++position;
    if (*position++ != ':')
        return NULL;
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')
        ++position;
    if (*position++ != '"')
        return NULL;
    const char* value_end = strchr(position, '"');
    if (value_end == NULL || (end != NULL && value_end > end))
        return NULL;
    return strndup(position, (size_t)(value_end - position));
}

static char* json_string(const char* document, const char* key) {
    return json_string_between(document, NULL, key);
}

static char* candidate_for_path(const char* topology, const char* expected_path) {
    const char* candidate = topology;
    while ((candidate = strstr(candidate, "\"candidateId\"")) != NULL) {
        const char* next = strstr(candidate + 1, "\"candidateId\"");
        char* path = json_string_between(candidate, next, "path");
        char* identifier = json_string_between(candidate, next, "candidateId");
        if (path != NULL && identifier != NULL && strcmp(path, expected_path) == 0) {
            free(path);
            return identifier;
        }
        free(path);
        free(identifier);
        candidate = next;
        if (candidate == NULL)
            break;
    }
    return NULL;
}

static char* start_preparation(sd_bus* bus, const char* request, int passphrase_fd) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* message = NULL;
    sd_bus_message* reply = NULL;
    if (sd_bus_message_new_method_call(bus, &message, service, object, interface, "StartDevicePreparation") < 0 ||
        sd_bus_message_append(message, "sh", request, passphrase_fd) < 0)
        die("cannot construct StartDevicePreparation call");
    const int result = sd_bus_call(bus, message, 0, &error, &reply);
    sd_bus_message_unref(message);
    if (result < 0) {
        fprintf(
            stderr,
            "device provisioning client: StartDevicePreparation failed: %s\n",
            error.message != NULL ? error.message : strerror(-result)
        );
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        exit(EXIT_FAILURE);
    }
    char* payload = reply_payload(reply);
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return payload;
}

int main(int argc, char** argv) {
    if (argc != 4)
        die("usage: DeviceProvisioningClient PARTITION SOURCE PASSPHRASE_FILE");
    sd_bus* bus = NULL;
    if (sd_bus_open_system(&bus) < 0)
        die("cannot connect to the system bus");

    char* topology = call(bus, "InspectStorageTopology", NULL, NULL);
    char* generation = json_string(topology, "generation");
    char* candidate = candidate_for_path(topology, argv[1]);
    if (generation == NULL || candidate == NULL)
        die("selected partition is absent from storage topology");

    char plan_request[2048];
    if (snprintf(
            plan_request,
            sizeof(plan_request),
            "{\"topologyGeneration\":\"%s\",\"candidateId\":\"%s\",\"mode\":\"reformat-existing-partition\"}",
            generation,
            candidate
        ) >= (int)sizeof(plan_request))
        die("plan request is too large");
    char* plan = call(bus, "BuildDevicePreparationPlan", "s", plan_request);
    char* plan_id = json_string(plan, "planId");
    if (plan_id == NULL)
        die("manager omitted the preparation plan identifier");

    char start_request[4096];
    if (snprintf(
            start_request,
            sizeof(start_request),
            "{\"profileId\":\"partition-integration\",\"profileName\":\"Partition integration\","
            "\"planId\":\"%s\",\"sourceSubvolume\":\"%s\",\"passphraseLabel\":\"Integration\","
            "\"createAutomaticKey\":false}",
            plan_id,
            argv[2]
        ) >= (int)sizeof(start_request))
        die("start request is too large");
    const int passphrase_fd = open(argv[3], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (passphrase_fd < 0)
        die("cannot open the passphrase file");
    char* status = start_preparation(bus, start_request, passphrase_fd);
    close(passphrase_fd);
    char* operation_id = json_string(status, "operationId");
    if (operation_id == NULL)
        die("manager omitted the preparation operation identifier");

    for (int attempt = 0; attempt < 600; ++attempt) {
        free(status);
        status = call(bus, "GetDevicePreparation", "s", operation_id);
        char* state = json_string(status, "state");
        if (state == NULL)
            die("manager omitted the preparation state");
        if (strcmp(state, "succeeded") == 0) {
            free(state);
            puts(status);
            free(status);
            free(operation_id);
            free(plan_id);
            free(plan);
            free(candidate);
            free(generation);
            free(topology);
            sd_bus_unref(bus);
            return EXIT_SUCCESS;
        }
        if (strcmp(state, "failed") == 0 || strcmp(state, "cancelled") == 0) {
            fprintf(stderr, "%s\n", status);
            free(state);
            die("device preparation did not succeed");
        }
        free(state);
        usleep(100000);
    }
    die("device preparation timed out");
}
