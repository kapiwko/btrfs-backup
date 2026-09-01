// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/CryptsetupOperations.hpp>

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <config/json/Json.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::storage {

namespace {

std::string descriptor_path(int fd) {
    if (fd < 0)
        throw ValidationError("credential descriptor is invalid");
    return "/proc/self/fd/" + std::to_string(fd);
}

void rewind_descriptor(int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0)
        throw ValidationError(std::string("cannot rewind credential descriptor: ") + std::strerror(errno));
}

std::string diagnostic(const btrfsbackup::backup::CommandResult& result, const char* operation) {
    if (result.timed_out)
        return std::string(operation) + " timed out";
    if (result.cancelled)
        return std::string(operation) + " was cancelled";
    return std::string(operation) + " failed";
}

} // namespace

CryptsetupOperations::CryptsetupOperations(btrfsbackup::backup::ICommandRunner& commands)
    : commands_(commands) {
}

void CryptsetupOperations::require_success(
    const std::vector<std::string>& command,
    const btrfsbackup::backup::ControlledCommandOptions& options,
    const char* operation
) {
    const btrfsbackup::backup::CommandResult result = commands_.run_controlled(command, options);
    if (result.exit_code != 0 || result.cancelled || result.timed_out)
        throw ValidationError(diagnostic(result, operation));
}

LuksHeader CryptsetupOperations::inspect_luks2(const std::filesystem::path& device) {
    if (!device.is_absolute() || device.lexically_normal() != device)
        throw ValidationError("LUKS device path is invalid");
    const auto luks2 = commands_.run({"cryptsetup", "isLuks", "--type", "luks2", device.string()});
    if (luks2.exit_code != 0)
        throw ValidationError("target is not a LUKS2 device");
    const std::string uuid = btrfsbackup::backup::capture_command(
        commands_,
        {"cryptsetup", "luksUUID", device.string()}
    );
    const std::string dump = btrfsbackup::backup::capture_command(
        commands_,
        {"cryptsetup", "luksDump", "--dump-json-metadata", device.string()}
    );
    const btrfsbackup::config::json::Json document = btrfsbackup::config::json::Json::parse(dump);
    if (!document.is_object() || !document.contains("keyslots") || !document.at("keyslots").is_object())
        throw ValidationError("cryptsetup returned invalid LUKS2 metadata");
    std::vector<int> keyslots;
    for (const auto& [key, value] : document.at("keyslots").items()) {
        static_cast<void>(value);
        try {
            std::size_t consumed = 0;
            const int slot = std::stoi(key, &consumed);
            if (consumed != key.size() || slot < 0)
                throw ValidationError("cryptsetup returned an invalid keyslot");
            keyslots.push_back(slot);
        } catch (const std::invalid_argument&) {
            throw ValidationError("cryptsetup returned an invalid keyslot");
        } catch (const std::out_of_range&) {
            throw ValidationError("cryptsetup returned an invalid keyslot");
        }
    }
    std::ranges::sort(keyslots);
    return {.uuid = uuid, .keyslots = std::move(keyslots)};
}

void CryptsetupOperations::add_key(
    const std::filesystem::path& device,
    int authorization_fd,
    int new_key_fd
) {
    rewind_descriptor(authorization_fd);
    rewind_descriptor(new_key_fd);
    btrfsbackup::backup::ControlledCommandOptions options;
    options.inherited_fds = {authorization_fd, new_key_fd};
    require_success(
        {
            "cryptsetup",
            "luksAddKey",
            "--batch-mode",
            "--key-file",
            descriptor_path(authorization_fd),
            device.string(),
            descriptor_path(new_key_fd),
        },
        options,
        "adding a LUKS credential"
    );
}

void CryptsetupOperations::test_key(const std::filesystem::path& device, int key_fd) {
    rewind_descriptor(key_fd);
    btrfsbackup::backup::ControlledCommandOptions options;
    options.inherited_fds = {key_fd};
    require_success(
        {"cryptsetup", "open", "--test-passphrase", "--key-file", descriptor_path(key_fd), device.string()},
        options,
        "testing a LUKS credential"
    );
}

void CryptsetupOperations::remove_keyslot(
    const std::filesystem::path& device,
    int keyslot,
    int authorization_fd
) {
    if (keyslot < 0)
        throw ValidationError("LUKS keyslot is invalid");
    rewind_descriptor(authorization_fd);
    btrfsbackup::backup::ControlledCommandOptions options;
    options.inherited_fds = {authorization_fd};
    require_success(
        {
            "cryptsetup",
            "luksKillSlot",
            "--batch-mode",
            "--key-file",
            descriptor_path(authorization_fd),
            device.string(),
            std::to_string(keyslot),
        },
        options,
        "removing a LUKS credential"
    );
}

} // namespace btrfsbackup::platform::linux::storage
