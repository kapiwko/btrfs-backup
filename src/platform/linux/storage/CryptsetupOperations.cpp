// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/CryptsetupOperations.hpp>

#include <libcryptsetup.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <memory>
#include <ranges>
#include <utility>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::storage {
namespace {

struct CryptDeviceDeleter {
    void operator()(crypt_device* device) const noexcept {
        crypt_free(device);
    }
};

using OwnedCryptDevice = std::unique_ptr<crypt_device, CryptDeviceDeleter>;

void require_result(int result, const char* operation) {
    if (result < 0)
        throw ValidationError(std::string(operation) + " failed: " + std::strerror(-result));
}

void validate_device_path(const std::filesystem::path& device) {
    if (!device.is_absolute() || device.lexically_normal() != device)
        throw ValidationError("LUKS device path is invalid");
}

void validate_mapper(const std::string& mapper) {
    if (mapper.empty() || mapper.size() > 127 || !std::ranges::all_of(mapper, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '-' || character == '_' || character == '.';
        }))
        throw ValidationError("LUKS mapper name is invalid");
}

class SafeSecret {
  public:
    explicit SafeSecret(int fd) {
        if (fd < 0)
            throw ValidationError("credential descriptor is invalid");
        if (::lseek(fd, 0, SEEK_SET) < 0)
            throw ValidationError(std::string("cannot rewind credential descriptor: ") + std::strerror(errno));
        data_ = crypt_safe_alloc(maximum_size + 1);
        if (data_ == nullptr)
            throw ValidationError("cannot allocate protected credential memory");
        try {
            while (size_ <= maximum_size) {
                const ssize_t count = ::read(
                    fd,
                    static_cast<unsigned char*>(data_) + size_,
                    maximum_size + 1 - size_
                );
                if (count > 0) {
                    size_ += static_cast<std::size_t>(count);
                    continue;
                }
                if (count == 0)
                    break;
                if (errno == EINTR)
                    continue;
                throw ValidationError(std::string("cannot read credential descriptor: ") + std::strerror(errno));
            }
            if (size_ == 0)
                throw ValidationError("credential must not be empty");
            if (size_ > maximum_size)
                throw ValidationError("credential exceeds the accepted size");
        } catch (...) {
            crypt_safe_free(std::exchange(data_, nullptr));
            throw;
        }
    }

    ~SafeSecret() noexcept {
        crypt_safe_free(data_);
    }
    SafeSecret(const SafeSecret&) = delete;
    SafeSecret& operator=(const SafeSecret&) = delete;

    [[nodiscard]] const char* data() const noexcept {
        return static_cast<const char*>(data_);
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

  private:
    static constexpr std::size_t maximum_size = 4096;
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

OwnedCryptDevice initialize(const std::filesystem::path& device, bool load) {
    validate_device_path(device);
    crypt_device* raw = nullptr;
    const int initialization = crypt_init(&raw, device.c_str());
    OwnedCryptDevice result(raw);
    require_result(initialization, "initializing LUKS device");
    if (load)
        require_result(crypt_load(result.get(), CRYPT_LUKS2, nullptr), "loading LUKS2 metadata");
    return result;
}

std::string uuid(crypt_device* device) {
    const char* value = crypt_get_uuid(device);
    if (value == nullptr || *value == '\0')
        throw ValidationError("LUKS2 UUID is unavailable");
    return value;
}

int activate_secret(crypt_device* device, const char* mapper, const SafeSecret& secret) {
    return crypt_activate_by_passphrase(
        device,
        mapper,
        CRYPT_ANY_SLOT,
        secret.data(),
        secret.size(),
        0
    );
}

} // namespace

LuksHeader CryptsetupOperations::inspect_luks2(const std::filesystem::path& device) {
    auto context = initialize(device, true);
    const int maximum = crypt_keyslot_max(CRYPT_LUKS2);
    require_result(maximum, "reading LUKS2 keyslot limit");
    std::vector<int> keyslots;
    for (int slot = 0; slot < maximum; ++slot) {
        const auto status = crypt_keyslot_status(context.get(), slot);
        if (status == CRYPT_SLOT_ACTIVE || status == CRYPT_SLOT_ACTIVE_LAST || status == CRYPT_SLOT_UNBOUND)
            keyslots.push_back(slot);
    }
    return {.uuid = uuid(context.get()), .keyslots = std::move(keyslots)};
}

void CryptsetupOperations::add_key(
    const std::filesystem::path& device,
    int authorization_fd,
    int new_key_fd
) {
    auto context = initialize(device, true);
    const SafeSecret authorization(authorization_fd);
    const SafeSecret new_key(new_key_fd);
    require_result(
        crypt_keyslot_add_by_passphrase(
            context.get(),
            CRYPT_ANY_SLOT,
            authorization.data(),
            authorization.size(),
            new_key.data(),
            new_key.size()
        ),
        "adding a LUKS credential"
    );
}

void CryptsetupOperations::test_key(const std::filesystem::path& device, int key_fd) {
    auto context = initialize(device, true);
    const SafeSecret secret(key_fd);
    require_result(activate_secret(context.get(), nullptr, secret), "testing a LUKS credential");
}

void CryptsetupOperations::remove_keyslot(
    const std::filesystem::path& device,
    int keyslot,
    int authorization_fd
) {
    if (keyslot < 0)
        throw ValidationError("LUKS keyslot is invalid");
    auto context = initialize(device, true);
    const SafeSecret authorization(authorization_fd);
    require_result(
        activate_secret(context.get(), nullptr, authorization),
        "authorizing LUKS credential removal"
    );
    require_result(crypt_keyslot_destroy(context.get(), keyslot), "removing a LUKS credential");
}

std::filesystem::path CryptsetupOperations::active_device(const std::string& mapper) {
    validate_mapper(mapper);
    crypt_device* raw = nullptr;
    const int initialization = crypt_init_by_name(&raw, mapper.c_str());
    OwnedCryptDevice context(raw);
    require_result(initialization, "opening active LUKS mapping");
    const char* device = crypt_get_device_name(context.get());
    if (device == nullptr || *device == '\0')
        throw ValidationError("active LUKS device is unavailable");
    return device;
}

std::string CryptsetupOperations::format_luks2(const std::filesystem::path& device, int key_fd) {
    auto context = initialize(device, false);
    require_result(
        crypt_format(context.get(), CRYPT_LUKS2, "aes", "xts-plain64", nullptr, nullptr, 64, nullptr),
        "formatting LUKS2"
    );
    const SafeSecret secret(key_fd);
    require_result(
        crypt_keyslot_add_by_volume_key(
            context.get(),
            CRYPT_ANY_SLOT,
            nullptr,
            0,
            secret.data(),
            secret.size()
        ),
        "creating the initial LUKS2 keyslot"
    );
    return uuid(context.get());
}

void CryptsetupOperations::open_luks2(
    const std::filesystem::path& device,
    const std::string& mapper,
    int key_fd
) {
    validate_mapper(mapper);
    auto context = initialize(device, true);
    const SafeSecret secret(key_fd);
    require_result(activate_secret(context.get(), mapper.c_str(), secret), "opening LUKS2 target");
}

void CryptsetupOperations::close(const std::string& mapper) {
    validate_mapper(mapper);
    require_result(crypt_deactivate(nullptr, mapper.c_str()), "closing LUKS2 target");
}

} // namespace btrfsbackup::platform::linux::storage
