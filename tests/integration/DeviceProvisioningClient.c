// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE

#include <systemd/sd-bus.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

static int json_true_between(const char* begin, const char* end, const char* key) {
    char pattern[128];
    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern))
        die("JSON key is too long");
    const char* position = strstr(begin, pattern);
    if (position == NULL || (end != NULL && position >= end))
        return 0;
    position += strlen(pattern);
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')
        ++position;
    if (*position++ != ':')
        return 0;
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')
        ++position;
    return strncmp(position, "true", 4) == 0;
}

static long json_integer_between(const char* begin, const char* end, const char* key) {
    char pattern[128];
    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern))
        die("JSON key is too long");
    const char* position = strstr(begin, pattern);
    if (position == NULL || (end != NULL && position >= end))
        return -1;
    position += strlen(pattern);
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')
        ++position;
    if (*position++ != ':')
        return -1;
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n')
        ++position;
    char* value_end = NULL;
    const long value = strtol(position, &value_end, 10);
    if (value_end == position || (end != NULL && value_end > end))
        return -1;
    return value;
}

static long partition_number_from_path(const char* path) {
    const char* end = path + strlen(path);
    const char* begin = end;
    while (begin > path && begin[-1] >= '0' && begin[-1] <= '9')
        --begin;
    if (begin == end)
        return -1;
    return strtol(begin, NULL, 10);
}

struct BlockGeometry {
    unsigned long long size_bytes;
    unsigned int logical_sector_size;
};

static struct BlockGeometry block_geometry(const char* path) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        die("cannot open the selected block device");
    struct BlockGeometry geometry = {0};
    if (ioctl(fd, BLKGETSIZE64, &geometry.size_bytes) < 0 ||
        ioctl(fd, BLKSSZGET, &geometry.logical_sector_size) < 0 || geometry.logical_sector_size == 0) {
        close(fd);
        die("cannot read the selected block device geometry");
    }
    close(fd);
    return geometry;
}

static char* partition_candidate(
    const char* topology,
    long expected_partition_number,
    struct BlockGeometry expected_geometry
) {
    const char* candidate = topology;
    while ((candidate = strstr(candidate, "\"candidateId\"")) != NULL) {
        const char* next = strstr(candidate + 1, "\"candidateId\"");
        char* kind = json_string_between(candidate, next, "kind");
        char* identifier = json_string_between(candidate, next, "candidateId");
        if (kind != NULL && identifier != NULL && strcmp(kind, "existing-partition") == 0 &&
            json_integer_between(candidate, next, "partitionNumber") == expected_partition_number &&
            (unsigned long long)json_integer_between(candidate, next, "sectorCount") *
                    expected_geometry.logical_sector_size ==
                expected_geometry.size_bytes &&
            json_true_between(candidate, next, "suitableForReformat")) {
            free(kind);
            return identifier;
        }
        free(kind);
        free(identifier);
        candidate = next;
        if (candidate == NULL)
            break;
    }
    return NULL;
}

static char* source_candidate_for_path(const char* candidates, const char* expected_path) {
    const char* candidate = candidates;
    while ((candidate = strstr(candidate, "\"id\"")) != NULL) {
        const char* next = strstr(candidate + 1, "\"id\"");
        char* path = json_string_between(candidate, next, "path");
        char* identifier = json_string_between(candidate, next, "id");
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

static char* unallocated_candidate(const char* topology, unsigned long long expected_device_size) {
    const char* device = topology;
    while ((device = strstr(device, "\"displayIndex\"")) != NULL) {
        const char* next_device = strstr(device + 1, "\"displayIndex\"");
        if ((unsigned long long)json_integer_between(device, next_device, "sizeBytes") == expected_device_size) {
            const char* region = device;
            while ((region = strstr(region, "\"candidateId\"")) != NULL &&
                   (next_device == NULL || region < next_device)) {
                const char* next_region = strstr(region + 1, "\"candidateId\"");
                if (next_device != NULL && (next_region == NULL || next_region > next_device))
                    next_region = next_device;
                char* kind = json_string_between(region, next_region, "kind");
                char* identifier = json_string_between(region, next_region, "candidateId");
                if (kind != NULL && identifier != NULL && strcmp(kind, "unallocated") == 0 &&
                    json_true_between(region, next_region, "suitableForBackupPartition")) {
                    free(kind);
                    return identifier;
                }
                free(kind);
                free(identifier);
                region = next_region;
                if (region == NULL || region == next_device)
                    break;
            }
        }
        device = next_device;
        if (device == NULL)
            break;
    }
    return NULL;
}

static char* device_candidate(const char* topology, unsigned long long expected_device_size) {
    const char* display_index = topology;
    while ((display_index = strstr(display_index, "\"displayIndex\"")) != NULL) {
        const char* next_device = strstr(display_index + 1, "\"displayIndex\"");
        if ((unsigned long long)json_integer_between(display_index, next_device, "sizeBytes") ==
            expected_device_size) {
            const char* candidate = topology;
            const char* selected = NULL;
            while ((candidate = strstr(candidate, "\"candidateId\"")) != NULL && candidate < display_index) {
                selected = candidate;
                ++candidate;
            }
            if (selected != NULL)
                return json_string_between(selected, display_index, "candidateId");
        }
        display_index = next_device;
        if (display_index == NULL)
            break;
    }
    return NULL;
}

static char* call_with_fd(sd_bus* bus, const char* method, const char* request, int descriptor) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* message = NULL;
    sd_bus_message* reply = NULL;
    if (sd_bus_message_new_method_call(bus, &message, service, object, interface, method) < 0 ||
        sd_bus_message_append(message, "sh", request, descriptor) < 0)
        die("cannot construct descriptor-bearing method call");
    const int result = sd_bus_call(bus, message, 0, &error, &reply);
    sd_bus_message_unref(message);
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

int main(int argc, char** argv) {
    if (argc < 4 || argc > 6)
        die("usage: DeviceProvisioningClient TARGET SOURCE PASSPHRASE_FILE [MODE [PROFILE_ID]]");
    const char* mode = argc >= 5 ? argv[4] : "reformat-existing-partition";
    const char* profile_id = argc >= 6 ? argv[5] : "partition-integration";
    sd_bus* bus = NULL;
    if (sd_bus_open_system(&bus) < 0)
        die("cannot connect to the system bus");

    char* topology = call(bus, "InspectStorageTopology", NULL, NULL);
    char* generation = json_string(topology, "generation");
    const struct BlockGeometry target_geometry = block_geometry(argv[1]);
    char* candidate = NULL;
    if (strcmp(mode, "erase-whole-device") == 0)
        candidate = device_candidate(topology, target_geometry.size_bytes);
    else if (strcmp(mode, "create-partition-in-unallocated-space") == 0)
        candidate = unallocated_candidate(topology, target_geometry.size_bytes);
    else
        candidate = partition_candidate(topology, partition_number_from_path(argv[1]), target_geometry);
    if (generation == NULL || candidate == NULL)
        die("selected storage target is absent from storage topology");

    char* inspection_id = NULL;
    if (strcmp(mode, "adopt-existing-target") == 0) {
        char inspection_request[2048];
        if (snprintf(
                inspection_request,
                sizeof(inspection_request),
                "{\"topologyGeneration\":\"%s\",\"candidateId\":\"%s\"}",
                generation,
                candidate
            ) >= (int)sizeof(inspection_request))
            die("inspection request is too large");
        const int inspection_fd = open(argv[3], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (inspection_fd < 0)
            die("cannot open the inspection passphrase file");
        char* inspection = call_with_fd(bus, "InspectExistingTarget", inspection_request, inspection_fd);
        close(inspection_fd);
        inspection_id = json_string(inspection, "inspectionId");
        char* classification = json_string(inspection, "classification");
        if (inspection_id == NULL || *inspection_id == '\0' || classification == NULL ||
            strcmp(classification, "compatible-repository") != 0)
            die("selected storage target is not an adoptable repository");
        free(classification);
        free(inspection);
    }

    char plan_request[2048];
    if (snprintf(
            plan_request,
            sizeof(plan_request),
            "{\"topologyGeneration\":\"%s\",\"candidateId\":\"%s\",\"mode\":\"%s\","
            "\"inspectionId\":\"%s\"}",
            generation,
            candidate,
            mode,
            inspection_id != NULL ? inspection_id : ""
        ) >= (int)sizeof(plan_request))
        die("plan request is too large");
    char* plan = call(bus, "BuildDevicePreparationPlan", "s", plan_request);
    char* plan_id = json_string(plan, "planId");
    if (plan_id == NULL)
        die("manager omitted the preparation plan identifier");
    char* sources = call(bus, "ListSourceCandidates", NULL, NULL);
    char* source_candidate = source_candidate_for_path(sources, argv[2]);
    if (source_candidate == NULL)
        die("selected source is absent from source candidates");

    char start_request[4096];
    if (snprintf(
            start_request,
            sizeof(start_request),
            "{\"profileId\":\"%s\",\"profileName\":\"Partition integration\","
            "\"planId\":\"%s\",\"sourceCandidateId\":\"%s\",\"passphraseLabel\":\"Integration\","
            "\"createAutomaticKey\":false}",
            profile_id,
            plan_id,
            source_candidate
        ) >= (int)sizeof(start_request))
        die("start request is too large");
    const int passphrase_fd = open(argv[3], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (passphrase_fd < 0)
        die("cannot open the passphrase file");
    char* status = call_with_fd(bus, "StartDevicePreparation", start_request, passphrase_fd);
    free(source_candidate);
    free(sources);
    close(passphrase_fd);
    char* operation_id = json_string(status, "operationId");
    if (operation_id == NULL)
        die("manager omitted the preparation operation identifier");

    for (int attempt = 0; attempt < 1800; ++attempt) {
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
            free(inspection_id);
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
