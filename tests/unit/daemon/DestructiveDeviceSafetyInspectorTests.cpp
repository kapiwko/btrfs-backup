// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>

#include <backup/ports/ICommandRunner.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "support/TestHelpers.hpp"

namespace {

namespace backup = btrfsbackup::backup;
using btrfsbackup::daemon::control::DestructiveDeviceSafetyInspector;
using btrfsbackup::daemon::control::ProvisioningDevice;

class Commands final : public backup::ICommandRunner {
  public:
    std::string graph;
    backup::CommandResult run(const std::vector<std::string>& argv) override {
        if (!argv.empty() && argv.front() == "lsblk")
            return {0, graph};
        return {1, {}};
    }
    backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const backup::ControlledCommandOptions&
    ) override {
        return run(argv);
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

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::ranges::find(values, value) != values.end();
}

void test_reports_every_detected_usage_reason() {
    const auto root = test_helpers::test_root("destructive-device-safety", "unsafe");
    std::filesystem::create_directories(root / "sys/8:16/holders");
    std::filesystem::create_directories(root / "sys/8:17/holders");
    std::ofstream(root / "sys/8:16/holders/dm-0") << '\n';
    std::ofstream(root / "swaps")
        << "Filename Type Size Used Priority\n/dev/test1 partition 1024 0 -2\n";
    Commands commands;
    commands.graph = R"({"blockdevices":[{"path":"/dev/test","type":"disk","maj:min":"8:16","pkname":null,"mountpoints":[],"children":[{"path":"/dev/test1","type":"part","maj:min":"8:17","pkname":"/dev/test","mountpoints":["/"],"children":[{"path":"/dev/mapper/root","type":"crypt","maj:min":"253:0","pkname":"/dev/test1","mountpoints":["/"]}]}]}]})";
    DestructiveDeviceSafetyInspector inspector(
        commands,
        root / "swaps",
        root / "sys",
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
    const auto root = test_helpers::test_root("destructive-device-safety", "safe");
    std::filesystem::create_directories(root / "sys/8:16/holders");
    std::ofstream(root / "swaps") << "Filename Type Size Used Priority\n";
    Commands commands;
    commands.graph = R"({"blockdevices":[{"path":"/dev/test","type":"disk","maj:min":"8:16","pkname":null,"mountpoints":[]}]})";
    DestructiveDeviceSafetyInspector inspector(
        commands,
        root / "swaps",
        root / "sys",
        [](const ProvisioningDevice&) { return std::optional<std::string>{}; }
    );
    test_helpers::expect_true(
        "unused device",
        inspector.inspect(candidate()).empty(),
        "unused device was rejected"
    );
}

void test_unavailable_safety_sources_fail_closed() {
    const auto root = test_helpers::test_root("destructive-device-safety", "unavailable");
    Commands commands;
    commands.graph = "invalid-json";
    DestructiveDeviceSafetyInspector inspector(
        commands,
        root / "missing-swaps",
        root / "missing-sys",
        [](const ProvisioningDevice&) { return std::optional<std::string>{}; }
    );
    const auto reasons = inspector.inspect(candidate());
    test_helpers::expect_true(
        "missing graph",
        contains(reasons, "block-graph-unavailable"),
        "unavailable block graph was accepted"
    );
    test_helpers::expect_true(
        "missing swaps",
        contains(reasons, "swap-state-unavailable"),
        "unavailable swap state was accepted"
    );
    test_helpers::expect_true(
        "missing holders",
        contains(reasons, "holder-state-unavailable:8:16"),
        "unavailable holder state was accepted"
    );
}

} // namespace

int main() {
    test_reports_every_detected_usage_reason();
    test_accepts_complete_unused_device();
    test_unavailable_safety_sources_fail_closed();
    return test_helpers::finish("destructive device safety inspector tests");
}
