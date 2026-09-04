// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransactionStore.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/control/DevicePreparationTransactionCodec.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <state/document/BoundedDocumentReader.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

constexpr mode_t transaction_permissions = S_IRUSR | S_IWUSR;
constexpr std::size_t maximum_transaction_size = 1024 * 1024;
constexpr fs::perms transaction_directory_permissions =
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec;

class TransactionLock final {
  public:
    explicit TransactionLock(const fs::path& path) {
        int descriptor;
        do {
            descriptor = ::open(
                path.c_str(),
                O_RDWR | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW,
                transaction_permissions
            );
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0)
            throw ValidationError("cannot open device preparation transaction lock");
        descriptor_ = platform::linux::OwnedFileDescriptor(descriptor);
        struct stat status{};
        if (::fstat(descriptor_.get(), &status) < 0 || !S_ISREG(status.st_mode) ||
            status.st_uid != ::geteuid() || (status.st_mode & 0777) != transaction_permissions) {
            throw ValidationError("unsafe device preparation transaction lock");
        }
        int result;
        do {
            result = ::flock(descriptor_.get(), LOCK_EX);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            throw ValidationError("cannot lock device preparation transaction");
    }

  private:
    platform::linux::OwnedFileDescriptor descriptor_;
};

bool terminal(const DevicePreparationTransaction& transaction) {
    return transaction.profile_reservation_state != "held" &&
        transaction.profile_reservation_state != "releasing" &&
        (transaction.status.state == "succeeded" || transaction.status.state == "failed" ||
         transaction.status.state == "cancelled" || transaction.status.state == "interrupted");
}

bool state_transition_allowed(std::string_view from, std::string_view to) {
    if (from == to)
        return true;
    if (from == "queued")
        return to == "running" || to == "cancelled" || to == "interrupted" || to == "failed";
    if (from == "running")
        return to == "succeeded" || to == "cancelled" || to == "interrupted" || to == "failed";
    return false;
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

void prepare_root(const fs::path& root) {
    std::error_code error;
    const bool created = fs::create_directories(root, error);
    if (error)
        throw ValidationError("cannot create device preparation transaction directory");
    platform::linux::OwnedFileDescriptor descriptor(
        ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
    );
    if (!descriptor.valid())
        throw ValidationError("cannot open device preparation transaction directory");
    if (created && ::fchmod(descriptor.get(), static_cast<mode_t>(transaction_directory_permissions)) < 0)
        throw ValidationError("cannot secure device preparation transaction directory");
    struct stat status{};
    if (::fstat(descriptor.get(), &status) < 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != ::geteuid() ||
        (status.st_mode & 0777) != static_cast<mode_t>(transaction_directory_permissions)) {
        throw ValidationError("unsafe device preparation transaction directory");
    }
}

bool path_entry_exists(const fs::path& path) {
    struct stat status{};
    if (::lstat(path.c_str(), &status) == 0)
        return true;
    if (errno == ENOENT)
        return false;
    throw ValidationError("cannot inspect device preparation transaction path");
}

fs::path reservation_root(const fs::path& root) {
    return root / "profile-reservations";
}

fs::path transaction_lock_root(const fs::path& root) {
    return root / "transaction-locks";
}

fs::path transaction_path(const fs::path& root, const std::string& operation_id) {
    return root / (operation_id + ".json");
}

fs::path transaction_lock_path(const fs::path& root, const std::string& operation_id) {
    return transaction_lock_root(root) / (operation_id + ".lock");
}

fs::path reservation_path(const fs::path& root, const std::string& profile_id) {
    return reservation_root(root) / (profile_id + ".reservation");
}

[[noreturn]] void throw_reservation_error(const std::string& operation, const fs::path& path, int error) {
    throw ValidationError(operation + " " + path.string() + ": " + std::strerror(error));
}

void write_all(int descriptor, const std::string& value, const fs::path& path) {
    const char* current = value.data();
    std::size_t remaining = value.size();
    while (remaining > 0) {
        const std::size_t count = std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())
        );
        const ssize_t written = ::write(descriptor, current, count);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            throw_reservation_error("cannot write profile reservation", path, errno);
        }
        if (written == 0)
            throw ValidationError("cannot write profile reservation " + path.string());
        current += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

void fsync_file(int descriptor, const fs::path& path) {
    int result;
    do {
        result = ::fsync(descriptor);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        throw_reservation_error("cannot sync profile reservation", path, errno);
}

std::optional<std::string> read_reservation(const fs::path& path) {
    int descriptor;
    do {
        descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        if (errno == ENOENT)
            return std::nullopt;
        throw_reservation_error("cannot open profile reservation", path, errno);
    }
    platform::linux::OwnedFileDescriptor fd(descriptor);
    struct stat status{};
    if (::fstat(fd.get(), &status) < 0)
        throw_reservation_error("cannot inspect profile reservation", path, errno);
    if (!S_ISREG(status.st_mode) || status.st_uid != ::geteuid() ||
        (status.st_mode & 0777) != transaction_permissions || status.st_size <= 0 || status.st_size > 64) {
        throw ValidationError("invalid profile reservation " + path.string());
    }
    std::string owner(static_cast<std::size_t>(status.st_size), '\0');
    std::size_t offset = 0;
    while (offset < owner.size()) {
        const ssize_t count = ::read(fd.get(), owner.data() + offset, owner.size() - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            throw_reservation_error("cannot read profile reservation", path, errno);
        }
        if (count == 0)
            throw ValidationError("incomplete profile reservation " + path.string());
        offset += static_cast<std::size_t>(count);
    }
    validate_operation_id(owner);
    return owner;
}

DevicePreparationTransaction read_transaction(const fs::path& path) {
    const state::document::BoundedDocumentReader reader;
    return DevicePreparationTransactionCodec{}.deserialize(
        reader.read(path, maximum_transaction_size, ::geteuid(), transaction_permissions)
    );
}

} // namespace

DevicePreparationTransactionStore::DevicePreparationTransactionStore(
    fs::path root,
    std::size_t completed_limit,
    std::chrono::seconds completed_ttl
) : root_(std::move(root)), completed_limit_(completed_limit), completed_ttl_(completed_ttl) {
    if (root_.empty() || completed_limit_ == 0 || completed_ttl_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("invalid device preparation transaction retention");
}

void DevicePreparationTransactionStore::save(DevicePreparationTransaction& transaction) const {
    if (transaction.status.operation_id.empty())
        throw ValidationError("device preparation transaction has no operation identifier");
    validate_operation_id(transaction.status.operation_id);
    prepare_root(root_);
    prepare_root(transaction_lock_root(root_));
    const TransactionLock lock(transaction_lock_path(root_, transaction.status.operation_id));
    const fs::path path = transaction_path(root_, transaction.status.operation_id);
    DevicePreparationTransaction changed = transaction;
    if (path_entry_exists(path)) {
        const auto current = read_transaction(path);
        if (current.status.operation_id != transaction.status.operation_id)
            throw ValidationError("transaction file name does not match its operation identifier");
        if (current.revision != transaction.revision)
            throw ValidationError("device preparation transaction revision conflict");
        if (terminal(current))
            throw ValidationError("terminal device preparation transaction is immutable");
        if (!state_transition_allowed(current.status.state, transaction.status.state))
            throw ValidationError("invalid device preparation transaction state transition");
        if (current.cancel_requested && !transaction.cancel_requested)
            throw ValidationError("device preparation cancellation request cannot be cleared");
        if (transaction.revision.value == std::numeric_limits<std::uint64_t>::max())
            throw ValidationError("device preparation transaction revision is exhausted");
        changed.revision.value += 1;
    } else {
        if (transaction.revision.value != 0)
            throw ValidationError("device preparation transaction is missing");
        changed.revision.value = 1;
    }
    platform::linux::filesystem::atomic_write(
        path,
        DevicePreparationTransactionCodec{}.serialize(changed),
        transaction_permissions
    );
    transaction = std::move(changed);
}

DevicePreparationTransaction DevicePreparationTransactionStore::update(
    const std::string& operation_id,
    TransactionRevision expected_revision,
    const DevicePreparationTransition& transition
) const {
    validate_operation_id(operation_id);
    if (!transition)
        throw ValidationError("device preparation transaction transition is empty");
    prepare_root(root_);
    prepare_root(transaction_lock_root(root_));
    const TransactionLock lock(transaction_lock_path(root_, operation_id));
    const fs::path path = transaction_path(root_, operation_id);
    DevicePreparationTransaction current = read_transaction(path);
    if (current.status.operation_id != operation_id)
        throw ValidationError("transaction file name does not match its operation identifier");
    if (current.revision != expected_revision)
        throw ValidationError("device preparation transaction revision conflict");
    if (terminal(current))
        throw ValidationError("terminal device preparation transaction is immutable");
    DevicePreparationTransaction changed = current;
    transition(changed);
    if (changed.revision != current.revision || changed.status.operation_id != current.status.operation_id)
        throw ValidationError("device preparation transition changed immutable identity");
    if (!state_transition_allowed(current.status.state, changed.status.state))
        throw ValidationError("invalid device preparation transaction state transition");
    if (current.cancel_requested && !changed.cancel_requested)
        throw ValidationError("device preparation cancellation request cannot be cleared");
    if (changed.revision.value == std::numeric_limits<std::uint64_t>::max())
        throw ValidationError("device preparation transaction revision is exhausted");
    changed.revision.value += 1;
    platform::linux::filesystem::atomic_write(
        path,
        DevicePreparationTransactionCodec{}.serialize(changed),
        transaction_permissions
    );
    return changed;
}

DevicePreparationTransaction DevicePreparationTransactionStore::update(
    const std::string& operation_id,
    const DevicePreparationTransition& transition
) const {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto current = load(operation_id);
        try {
            return update(operation_id, current.revision, transition);
        } catch (const ValidationError& error) {
            if (std::string_view(error.what()) != "device preparation transaction revision conflict")
                throw;
        }
    }
    throw ValidationError("device preparation transaction remained busy");
}

DevicePreparationTransaction DevicePreparationTransactionStore::load(
    const std::string& operation_id
) const {
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path path = root_ / (operation_id + ".json");
    DevicePreparationTransaction transaction = read_transaction(path);
    if (transaction.status.operation_id != operation_id)
        throw ValidationError("transaction file name does not match its operation identifier");
    return transaction;
}

DevicePreparationTransactionScan DevicePreparationTransactionStore::load_and_prune() const {
    DevicePreparationTransactionScan scan;
    prepare_root(root_);
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(root_)) {
        if (entry.path().extension() != ".json")
            continue;
        try {
            DevicePreparationTransaction transaction = read_transaction(entry.path());
            if (entry.path().stem() != transaction.status.operation_id)
                throw ValidationError("transaction file name does not match its operation identifier");
            scan.transactions.push_back(std::move(transaction));
        } catch (const std::exception&) {
            const std::string operation_id = entry.path().stem().string();
            try {
                validate_operation_id(operation_id);
                scan.corrupted_operation_ids.push_back(operation_id);
            } catch (const std::exception&) {
            }
        }
    }

    const std::int64_t cutoff = now_seconds() - completed_ttl_.count();
    std::vector<DevicePreparationTransaction*> completed;
    for (auto& transaction : scan.transactions)
        if (terminal(transaction))
            completed.push_back(&transaction);
    std::ranges::sort(completed, std::greater{}, &DevicePreparationTransaction::updated_at);
    bool removed = false;
    for (std::size_t index = 0; index < completed.size(); ++index) {
        DevicePreparationTransaction& transaction = *completed.at(index);
        if (transaction.updated_at < cutoff || index >= completed_limit_) {
            fs::remove(root_ / (transaction.status.operation_id + ".json"), error);
            if (error)
                throw ValidationError("cannot prune device preparation transaction");
            removed = true;
            transaction.status.operation_id.clear();
        }
    }
    if (removed)
        platform::linux::filesystem::fsync_dir(root_);
    std::erase_if(scan.transactions, [](const auto& transaction) { return transaction.status.operation_id.empty(); });
    return scan;
}

void DevicePreparationTransactionStore::reserve_profile(
    const std::string& profile_id,
    const std::string& operation_id
) const {
    validate_profile_id(profile_id);
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path directory = reservation_root(root_);
    prepare_root(directory);
    const fs::path path = reservation_path(root_, profile_id);

    int descriptor;
    do {
        descriptor = ::open(
            path.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            transaction_permissions
        );
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        if (errno == EEXIST) {
            const auto owner = read_reservation(path);
            if (owner.has_value() && *owner == operation_id)
                return;
            throw ValidationError("profile is already reserved: " + profile_id);
        }
        throw_reservation_error("cannot create profile reservation", path, errno);
    }

    platform::linux::OwnedFileDescriptor fd(descriptor);
    try {
        write_all(fd.get(), operation_id, path);
        fsync_file(fd.get(), path);
        platform::linux::filesystem::fsync_dir(directory);
    } catch (...) {
        std::error_code ignored;
        fs::remove(path, ignored);
        throw;
    }
}

void DevicePreparationTransactionStore::release_profile(
    const std::string& profile_id,
    const std::string& operation_id
) const {
    validate_profile_id(profile_id);
    validate_operation_id(operation_id);
    prepare_root(root_);
    const fs::path directory = reservation_root(root_);
    prepare_root(directory);
    const fs::path path = reservation_path(root_, profile_id);
    const auto owner = read_reservation(path);
    if (!owner.has_value())
        return;
    if (*owner != operation_id)
        throw ValidationError("profile reservation is owned by another operation: " + profile_id);
    if (::unlink(path.c_str()) < 0) {
        if (errno == ENOENT)
            return;
        throw_reservation_error("cannot remove profile reservation", path, errno);
    }
    platform::linux::filesystem::fsync_dir(directory);
}

std::optional<std::string> DevicePreparationTransactionStore::profile_reservation_owner(
    const std::string& profile_id
) const {
    validate_profile_id(profile_id);
    prepare_root(root_);
    const fs::path directory = reservation_root(root_);
    prepare_root(directory);
    return read_reservation(reservation_path(root_, profile_id));
}

} // namespace btrfsbackup::daemon::control
