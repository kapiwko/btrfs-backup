// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemDeviceProvisioningBackend.hpp>

#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IBtrfsOperations.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>

#include <chrono>
#include <ranges>
#include <string_view>
#include <thread>
#include <unistd.h>

#include "support/TestHelpers.hpp"

namespace {
namespace backup = btrfsbackup::backup;
namespace config = btrfsbackup::config;
using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::DevicePreparationStatus;
using btrfsbackup::daemon::control::ICredentialAdministrationBackend;
using btrfsbackup::daemon::control::SystemDeviceProvisioningBackend;
using btrfsbackup::daemon::control::TargetCredential;

class Commands final : public backup::ICommandRunner {
  public:
    std::vector<std::vector<std::string>> calls;
    backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        if (argv.front() == "lsblk") {
            if (std::ranges::find(argv, "PATH,TYPE") != argv.end())
                return {0, R"({"blockdevices":[{"path":"/dev/test","type":"disk","children":[{"path":"/dev/test1","type":"part"}]}]})"};
            return {0, R"({"blockdevices":[{"path":"/dev/test","type":"disk","size":1048576,"model":"Test","serial":"SERIAL","tran":"usb","rm":true,"fstype":null,"pttype":null,"mountpoints":[null]}]})"};
        }
        if (argv.front() == "cryptsetup" && argv.at(1) == "luksUUID")
            return {0, "11111111-2222-3333-4444-555555555555\n"};
        if (argv.front() == "blkid" && argv.back() == "/dev/mapper/btrfs-backup-test")
            return {0, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n"};
        if (argv.front() == "blkid")
            return {0, "99999999-8888-7777-6666-555555555555\n"};
        return {0, {}};
    }
    backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const backup::ControlledCommandOptions&
    ) override {
        return run(argv);
    }
};

class Btrfs final : public backup::IBtrfsOperations {
  public:
    bool is_subvolume(const std::filesystem::path&) override {
        return true;
    }
    std::optional<backup::SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path&) override {
        return {};
    }
    void create_readonly_snapshot(const std::filesystem::path&, const std::filesystem::path&) override {
    }
    void delete_subvolume(const std::filesystem::path&) override {
    }
};

class Credentials final : public ICredentialAdministrationBackend {
  public:
    bool registered = false;
    bool generated = false;
    std::vector<TargetCredential> list_credentials(const ProfileId&) const override {
        return {};
    }
    void add_passphrase(const ProfileId&, int, int, const std::string&) override {
    }
    void add_key(const ProfileId&, int, int, const std::string&, bool) override {
    }
    void generate_key(const ProfileId&, int, const std::string&, bool automatic) override {
        generated = automatic;
    }
    void remove_credential(const ProfileId&, const std::string&, int) override {
    }
    void register_initial_passphrase(const ProfileId&, int slot, const std::string&) override {
        registered = slot == 0;
    }
};

void test_preparation_sequence_uses_descriptors_and_installs_profile() {
    const auto root = test_helpers::test_root("device-provisioning", "success");
    Commands commands;
    Btrfs btrfs;
    Credentials credentials;
    config::NullConfigurationActivator activator;
    SystemDeviceProvisioningBackend backend(
        {
            .config_root = root / "etc",
            .metadata_root = root / "etc/credentials",
            .key_root = root / "etc/keys",
            .lock_root = root / "run/locks",
            .udev_root = root / "udev",
            .systemd_root = root / "systemd",
            .public_root = root / "public",
        },
        root / "mnt",
        root / "mountinfo",
        commands,
        btrfs,
        activator,
        credentials
    );
    int descriptors[2];
    test_helpers::expect_true("secret pipe", ::pipe(descriptors) == 0, "cannot create pipe");
    constexpr std::string_view password = "secret";
    static_cast<void>(::write(descriptors[1], password.data(), password.size()));
    ::close(descriptors[1]);
    const auto started = backend.start(
        {"test", "Test backup", "/dev/test", "SERIAL", 1048576, "/home", "Recovery", true},
        descriptors[0]
    );
    ::close(descriptors[0]);
    DevicePreparationStatus status = started;
    for (int attempt = 0; attempt < 200 && status.state != "succeeded" && status.state != "failed"; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        status = backend.status(started.operation_id);
    }
    test_helpers::expect_eq("preparation state", status.state, "succeeded");
    test_helpers::expect_true("initial credential", credentials.registered, "initial passphrase not registered");
    test_helpers::expect_true("automatic credential", credentials.generated, "automatic key not generated");
    test_helpers::expect_true(
        "profile installed",
        std::filesystem::exists(root / "etc/profiles/test/profile.json"),
        "profile was not installed"
    );
    const auto wipe = std::ranges::find(commands.calls, std::string("wipefs"), [](const auto& call) { return call.front(); });
    const auto partition = std::ranges::find(commands.calls, std::string("sfdisk"), [](const auto& call) { return call.front(); });
    const auto format = std::ranges::find(commands.calls, std::string("mkfs.btrfs"), [](const auto& call) { return call.front(); });
    test_helpers::expect_true(
        "destructive command order",
        wipe != commands.calls.end() && partition != commands.calls.end() && format != commands.calls.end() &&
            wipe < partition && partition < format,
        "device commands ran out of order"
    );
}
} // namespace

int main() {
    test_preparation_sequence_uses_descriptors_and_installs_profile();
    return test_helpers::finish("system device provisioning backend tests");
}
