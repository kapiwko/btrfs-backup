// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>

#include <daemon/provisioning/StorageTopologyReader.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "support/TestHelpers.hpp"

namespace {

namespace provisioning = btrfsbackup::daemon::provisioning;
using btrfsbackup::daemon::control::DestructiveDeviceSafetyInspector;
using btrfsbackup::daemon::control::ProvisioningDevice;

class TopologyReader final : public provisioning::StorageTopologyReader {
  public:
    provisioning::StorageTopology topology;
    bool unavailable = false;

    provisioning::StorageTopology scan() override {
        if (unavailable)
            throw std::runtime_error("topology unavailable");
        return topology;
    }
};

ProvisioningDevice candidate() {
    return {
        .path = "/dev/test",
        .transport = "usb",
        .size_bytes = 1024,
        .major_minor = "8:16",
        .sysfs_devpath = "/devices/test/block/test",
        .serial_short = "SERIAL",
        .device_graph = "graph",
    };
}

provisioning::StorageTopology safe_topology() {
    provisioning::StorageDevice device;
    device.identity = {
        .display_path = "/dev/test",
        .major_minor = "8:16",
        .sysfs_path = "/devices/test/block/test",
        .serial_short = "SERIAL",
        .size_bytes = 1024,
    };
    device.transport = "usb";
    device.size_bytes = 1024;
    return {.generation = "generation-1", .devices = {std::move(device)}};
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::ranges::find(values, value) != values.end();
}

void test_reports_every_detected_usage_reason() {
    TopologyReader topology;
    topology.topology = safe_topology();
    provisioning::ExistingPartition partition;
    partition.identity = {
        .display_path = "/dev/test1",
        .major_minor = "8:17",
        .sysfs_path = "/devices/test/block/test/test1",
        .size_bytes = 512,
    };
    partition.mount_points = {"/"};
    partition.holders = {"dm-0"};
    partition.active_swap = true;
    partition.blockers = {{.code = "active-block-layer", .detail = "crypt"}};
    topology.topology.devices.front().regions.emplace_back(std::move(partition));
    DestructiveDeviceSafetyInspector inspector(
        topology,
        [](const ProvisioningDevice&) { return std::optional<std::string>{"exclusive-open-failed"}; }
    );
    const auto reasons = inspector.inspect(candidate());
    test_helpers::expect_true("swap reason", contains(reasons, "active-swap:/dev/test1"), "swap was not detected");
    test_helpers::expect_true("holder reason", contains(reasons, "block-holder:dm-0"), "holder was not detected");
    test_helpers::expect_true("stack reason", contains(reasons, "active-block-layer:crypt"), "crypt layer was not detected");
    test_helpers::expect_true("system reason", contains(reasons, "system-disk:/"), "system disk was not detected");
    test_helpers::expect_true("exclusive reason", contains(reasons, "exclusive-open-failed"), "exclusive open failure was ignored");
}

void test_accepts_complete_unused_device() {
    TopologyReader topology;
    topology.topology = safe_topology();
    DestructiveDeviceSafetyInspector inspector(
        topology,
        [](const ProvisioningDevice&) { return std::optional<std::string>{}; }
    );
    test_helpers::expect_true(
        "unused device",
        inspector.inspect(candidate()).empty(),
        "unused device was rejected"
    );
}

void test_unavailable_topology_fails_closed() {
    TopologyReader topology;
    topology.unavailable = true;
    DestructiveDeviceSafetyInspector inspector(
        topology,
        [](const ProvisioningDevice&) { return std::optional<std::string>{}; }
    );
    const auto reasons = inspector.inspect(candidate());
    test_helpers::expect_true(
        "missing graph",
        contains(reasons, "block-graph-unavailable"),
        "unavailable block graph was accepted"
    );
}

} // namespace

int main() {
    test_reports_every_detected_usage_reason();
    test_accepts_complete_unused_device();
    test_unavailable_topology_fails_closed();
    return test_helpers::finish("destructive device safety inspector tests");
}
