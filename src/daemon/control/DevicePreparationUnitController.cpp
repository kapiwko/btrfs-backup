// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationUnitController.hpp>

#include <array>
#include <cstddef>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/control/SystemdUnitController.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <platform/linux/transfer/ThreadSigpipeBlock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using platform::linux::OwnedFileDescriptor;

struct SecretBuffer final {
    std::vector<std::byte> bytes;

    SecretBuffer() = default;
    SecretBuffer(const SecretBuffer&) = delete;
    SecretBuffer& operator=(const SecretBuffer&) = delete;
    SecretBuffer(SecretBuffer&& other) noexcept : bytes(std::move(other.bytes)) {
    }
    ~SecretBuffer() noexcept {
        if (!bytes.empty())
            explicit_bzero(bytes.data(), bytes.size());
    }
};

std::string unit_name(const std::string& operation_id) {
    validate_operation_id(operation_id);
    return "btrfs-backup-device-preparation@" + operation_id + ".service";
}

void ensure_secret_root(const fs::path& root) {
    std::error_code error;
    fs::create_directories(root, error);
    if (error)
        throw ValidationError("cannot create device preparation secret directory");
    fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace, error);
    if (error)
        throw ValidationError("cannot secure device preparation secret directory");
}

void write_all(int descriptor, const std::byte* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t count = ::write(descriptor, data + offset, size - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        throw ValidationError("cannot deliver device preparation secret");
    }
}

unsigned int device_major(std::string_view major_minor) {
    const auto separator = major_minor.find(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == major_minor.size())
        throw ValidationError("device preparation major:minor is invalid");
    std::size_t parsed = 0;
    const unsigned long value = std::stoul(std::string(major_minor.substr(0, separator)), &parsed);
    if (parsed != separator)
        throw ValidationError("device preparation major:minor is invalid");
    parsed = 0;
    static_cast<void>(std::stoul(std::string(major_minor.substr(separator + 1)), &parsed));
    if (parsed != major_minor.size() - separator - 1)
        throw ValidationError("device preparation major:minor is invalid");
    return static_cast<unsigned int>(value);
}

std::string block_device_group(const fs::path& groups_path, unsigned int expected_major) {
    std::ifstream input(groups_path);
    if (!input)
        throw ValidationError("cannot read block device groups");
    bool block_devices = false;
    std::string line;
    while (std::getline(input, line)) {
        if (line == "Block devices:") {
            block_devices = true;
            continue;
        }
        if (!block_devices)
            continue;
        std::istringstream fields(line);
        unsigned int major = 0;
        std::string name;
        if (fields >> major >> name && major == expected_major)
            return name;
    }
    throw ValidationError("selected block device group is unavailable");
}

std::vector<std::string> device_properties(
    const DevicePreparationDeviceAccess& access,
    const fs::path& groups_path
) {
    if (access.major_minor.empty())
        throw ValidationError("device preparation has no allowed target device");
    std::set<std::string> devices;
    for (const auto& value : access.major_minor) {
        static_cast<void>(device_major(value));
        devices.insert(value);
    }
    std::vector<std::string> properties{"DevicePolicy=closed", "DeviceAllow="};
    for (const auto& value : devices)
        properties.push_back("DeviceAllow=/dev/block/" + value + " rw");
    if (access.allow_future_partitions) {
        const auto major = device_major(*devices.begin());
        const std::string selected_group = block_device_group(groups_path, major);
        properties.push_back("DeviceAllow=block-" + selected_group + " rw");
        if (selected_group != "blkext")
            properties.emplace_back("DeviceAllow=block-blkext rw");
    }
    properties.emplace_back("DeviceAllow=block-device-mapper rw");
    properties.emplace_back("DeviceAllow=/dev/mapper/control rw");
    return properties;
}

SecretBuffer read_secret(int descriptor) {
    if (::lseek(descriptor, 0, SEEK_SET) < 0)
        throw ValidationError("cannot rewind device preparation secret");
    SecretBuffer secret;
    secret.bytes.reserve(4096);
    std::array<std::byte, 512> buffer{};
    while (secret.bytes.size() <= 4096) {
        const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            secret.bytes.insert(secret.bytes.end(), buffer.begin(), buffer.begin() + count);
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        throw ValidationError("cannot read device preparation secret");
    }
    if (secret.bytes.empty() || secret.bytes.size() > 4096)
        throw ValidationError("device preparation secret has an invalid size");
    return secret;
}

} // namespace

SystemdDevicePreparationUnitController::SystemdDevicePreparationUnitController(
    ISystemdUnitController& units,
    fs::path secret_root,
    fs::path device_groups_path
) : units_(units), secret_root_(std::move(secret_root)), device_groups_path_(std::move(device_groups_path)) {
}

fs::path SystemdDevicePreparationUnitController::secret_path(const std::string& operation_id) const {
    validate_operation_id(operation_id);
    return secret_root_ / (operation_id + ".fifo");
}

void SystemdDevicePreparationUnitController::start_unit(
    const std::string& operation_id,
    const DevicePreparationDeviceAccess& access
) {
    const auto result = units_.start_unit({
        .unit = unit_name(operation_id),
        .timeout = std::chrono::seconds(30),
        .no_block = true,
        .runtime_properties = device_properties(access, device_groups_path_),
    });
    if (!result) {
        const std::string detail = result.error().detail.empty()
            ? std::string{}
            : ": " + result.error().detail;
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::TargetUnavailable,
            "cannot start device preparation helper" + detail
        );
    }
}

void SystemdDevicePreparationUnitController::start(
    const std::string& operation_id,
    int passphrase_fd,
    const DevicePreparationDeviceAccess& access
) {
    ensure_secret_root(secret_root_);
    const fs::path fifo = secret_path(operation_id);
    std::error_code error;
    fs::remove(fifo, error);
    if (::mkfifo(fifo.c_str(), S_IRUSR | S_IWUSR) < 0)
        throw ValidationError("cannot create device preparation secret channel");
    try {
        start_unit(operation_id, access);
        OwnedFileDescriptor writer;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            writer.reset(::open(fifo.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
            if (writer.valid())
                break;
            if (errno != ENXIO && errno != EINTR)
                throw ValidationError("cannot open device preparation secret channel");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!writer.valid())
            throw ValidationError("device preparation helper did not accept its secret");
        SecretBuffer secret = read_secret(passphrase_fd);
        platform::linux::transfer::ThreadSigpipeBlock sigpipe_block;
        write_all(writer.get(), secret.bytes.data(), secret.bytes.size());
    } catch (...) {
        fs::remove(fifo, error);
        try {
            stop(operation_id);
        } catch (...) {
        }
        throw;
    }
    fs::remove(fifo, error);
}

void SystemdDevicePreparationUnitController::recover(
    const std::string& operation_id,
    const DevicePreparationDeviceAccess& access
) {
    start_unit(operation_id, access);
}

void SystemdDevicePreparationUnitController::stop(const std::string& operation_id) {
    const auto result = units_.stop_unit({unit_name(operation_id), std::chrono::seconds(30)});
    if (!result)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::TargetUnavailable,
            "cannot stop device preparation helper"
        );
}

bool SystemdDevicePreparationUnitController::active(const std::string& operation_id) {
    const auto result =
        units_.active_unit({unit_name(operation_id), std::chrono::seconds(10)});
    return result.value_or(false);
}

} // namespace btrfsbackup::daemon::control
