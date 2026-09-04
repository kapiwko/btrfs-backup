// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>

#include <provisioning/StorageTopologyReader.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "support/TestHelpers.hpp"

namespace {

namespace provisioning = btrfsbackup::provisioning;
using btrfsbackup::daemon::control::DestructiveDeviceSafetyInspector;
using btrfsbackup::daemon::control::DevicePreparationTarget;
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

DevicePreparationTarget whole_device_target(const provisioning::StorageTopology& topology) {
    return {
        .mode = provisioning::ProvisioningMode::EraseWholeDevice,
        .device = topology.devices.front(),
    };
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
    const auto reasons = inspector.inspect(candidate(), whole_device_target(topology.topology));
    test_helpers::expect_true("swap reason", contains(reasons, "active-swap:/dev/test1"), "swap was not detected");
    test_helpers::expect_true("holder reason", contains(reasons, "block-holder:dm-0"), "holder was not detected");
    test_helpers::expect_true("stack reason", contains(reasons, "active-block-layer:crypt"), "crypt layer was not detected");
    test_helpers::expect_true("system reason", contains(reasons, "system-disk:/"), "system disk was not detected");
    test_helpers::expect_true("exclusive reason", contains(reasons, "exclusive-open-failed"), "exclusive open failure was ignored");
}

void test_rejects_lvm_and_md_member_signatures_before_exclusive_open() {
    for (const std::string signature : {"LVM2_member", "linux_raid_member"}) {
        TopologyReader topology;
        topology.topology = safe_topology();
        provisioning::ExistingPartition selected;
        selected.identity = {
            .display_path = "/dev/test1",
            .major_minor = "8:17",
            .sysfs_path = "/devices/test/block/test/test1",
            .size_bytes = 512,
        };
        selected.partition_uuid = "target";
        selected.partition_number = 1;
        selected.start_sector = 1;
        selected.sector_count = 1;
        selected.filesystem = {.type = signature};
        selected.blockers = {{.code = "unsupported-block-stack", .detail = signature}};
        topology.topology.devices.front().regions.emplace_back(selected);
        bool exclusive_probe_called = false;
        DestructiveDeviceSafetyInspector inspector(
            topology,
            [&](const ProvisioningDevice&) {
                exclusive_probe_called = true;
                return std::optional<std::string>{};
            }
        );
        const DevicePreparationTarget target{
            .mode = provisioning::ProvisioningMode::ReformatExistingPartition,
            .device = topology.topology.devices.front(),
            .partition = selected,
        };
        const auto reasons = inspector.inspect(candidate(), target);
        test_helpers::expect_true(
            "unsupported stack " + signature,
            contains(reasons, "unsupported-block-stack:" + signature),
            "an unsupported storage member reached destructive preparation"
        );
        test_helpers::expect_true(
            "unsupported stack still probed " + signature,
            exclusive_probe_called,
            "the final exclusive-open safety check was skipped"
        );
    }
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
        inspector.inspect(candidate(), whole_device_target(topology.topology)).empty(),
        "unused device was rejected"
    );
}

void test_partition_scope_ignores_mounted_sibling_and_probes_only_target() {
    TopologyReader topology;
    topology.topology = safe_topology();
    provisioning::ExistingPartition mounted_sibling;
    mounted_sibling.identity = {
        .display_path = "/dev/test1",
        .major_minor = "8:17",
        .sysfs_path = "/devices/test/block/test/test1",
        .size_bytes = 256,
    };
    mounted_sibling.partition_uuid = "sibling";
    mounted_sibling.partition_number = 1;
    mounted_sibling.start_sector = 1;
    mounted_sibling.sector_count = 1;
    mounted_sibling.mount_points = {"/media/data"};
    provisioning::ExistingPartition selected;
    selected.identity = {
        .display_path = "/dev/test2",
        .major_minor = "8:18",
        .sysfs_path = "/devices/test/block/test/test2",
        .size_bytes = 512,
    };
    selected.partition_uuid = "target";
    selected.partition_number = 2;
    selected.start_sector = 2;
    selected.sector_count = 1;
    selected.filesystem = {.type = "ext4", .uuid = "filesystem"};
    topology.topology.devices.front().regions = {
        provisioning::StorageRegion{mounted_sibling},
        provisioning::StorageRegion{selected},
    };
    std::string probed_path;
    DestructiveDeviceSafetyInspector inspector(
        topology,
        [&](const ProvisioningDevice& value) {
            probed_path = value.path;
            return std::optional<std::string>{};
        }
    );
    DevicePreparationTarget target{
        .mode = provisioning::ProvisioningMode::ReformatExistingPartition,
        .device = topology.topology.devices.front(),
        .partition = selected,
    };
    const auto reasons = inspector.inspect(candidate(), target);
    test_helpers::expect_true(
        "mounted sibling",
        reasons.empty(),
        "an unrelated mounted partition blocked the selected partition"
    );
    test_helpers::expect_eq("partition exclusive probe", probed_path, "/dev/test2");
}

void test_unavailable_topology_fails_closed() {
    TopologyReader topology;
    topology.unavailable = true;
    DestructiveDeviceSafetyInspector inspector(
        topology,
        [](const ProvisioningDevice&) { return std::optional<std::string>{}; }
    );
    const auto expected = safe_topology();
    const auto reasons = inspector.inspect(candidate(), whole_device_target(expected));
    test_helpers::expect_true(
        "missing graph",
        contains(reasons, "block-graph-unavailable"),
        "unavailable block graph was accepted"
    );
}

} // namespace

int main() {
    test_reports_every_detected_usage_reason();
    test_rejects_lvm_and_md_member_signatures_before_exclusive_open();
    test_accepts_complete_unused_device();
    test_partition_scope_ignores_mounted_sibling_and_probes_only_target();
    test_unavailable_topology_fails_closed();
    return test_helpers::finish("destructive device safety inspector tests");
}
