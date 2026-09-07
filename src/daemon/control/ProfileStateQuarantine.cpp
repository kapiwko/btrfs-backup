// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileStateQuarantine.hpp>

#include <system_error>
#include <utility>

#include <core/Errors.hpp>
#include <platform/linux/config/ProfileArtifactIo.hpp>
#include <platform/linux/filesystem/FileIo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {

ProfileStateQuarantine::ProfileStateQuarantine(
    ProfileStateRoots roots,
    std::string profile_id,
    std::string fingerprint
) : roots_(std::move(roots)) {
    const std::string transaction = std::move(fingerprint) + "-" +
        platform::linux::config::generate_configuration_generation().value();
    retired_profile_root_ = roots_.state_root / "retired" / profile_id;
    retired_transaction_root_ = retired_profile_root_ / transaction;
    transient_transaction_root_ = roots_.status_root / ".retired" / profile_id / transaction;
    moves_ = {
        {roots_.state_root / "profiles" / profile_id, retired_transaction_root_ / "state"},
        {roots_.history_root / profile_id, retired_transaction_root_ / "history"},
        {roots_.status_root / profile_id, transient_transaction_root_ / "status"},
    };
}

void ProfileStateQuarantine::create_private_directory(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (!error && status.type() != fs::file_type::not_found) {
        if (!fs::is_directory(status) || fs::is_symlink(status))
            throw ValidationError("profile state quarantine path is not a directory: " + path.string());
    } else if (error && error != std::errc::no_such_file_or_directory) {
        throw ValidationError("cannot inspect profile state quarantine path: " + path.string());
    } else {
        if (!fs::create_directory(path, error) || error)
            throw ValidationError("cannot create profile state quarantine directory: " + path.string());
    }
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, error);
    if (error)
        throw ValidationError("cannot secure profile state quarantine directory: " + path.string());
}

bool ProfileStateQuarantine::movable_directory_if_present(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return false;
    if (error)
        throw ValidationError("cannot inspect profile state directory: " + path.string());
    if (!fs::is_directory(status) || fs::is_symlink(status))
        throw ValidationError("profile state path is not a directory: " + path.string());
    return true;
}

void ProfileStateQuarantine::move_directory(Move& move) {
    if (!movable_directory_if_present(move.source))
        return;
    std::error_code error;
    fs::rename(move.source, move.destination, error);
    if (error) {
        throw ValidationError(
            "cannot quarantine profile state " + move.source.string() + ": " + error.message()
        );
    }
    move.moved = true;
    platform::linux::filesystem::fsync_dir(move.source.parent_path());
    platform::linux::filesystem::fsync_dir(move.destination.parent_path());
}

void ProfileStateQuarantine::quarantine() {
    try {
        const bool persistent_state = movable_directory_if_present(moves_[0].source);
        const bool persistent_history = movable_directory_if_present(moves_[1].source);
        const bool transient_status = movable_directory_if_present(moves_[2].source);

        if (persistent_state || persistent_history) {
            create_private_directory(roots_.state_root / "retired");
            create_private_directory(retired_profile_root_);
            create_private_directory(retired_transaction_root_);
        }
        if (transient_status) {
            create_private_directory(roots_.status_root / ".retired");
            create_private_directory(roots_.status_root / ".retired" / retired_profile_root_.filename());
            create_private_directory(transient_transaction_root_);
        }
        for (Move& move : moves_)
            move_directory(move);
    } catch (const std::exception& error) {
        const auto rollback_result = rollback();
        throw platform::linux::config::ConfigurationSaveError(error.what(), rollback_result);
    } catch (...) {
        const auto rollback_result = rollback();
        throw platform::linux::config::ConfigurationSaveError(
            "profile state quarantine failed with an unknown error",
            rollback_result
        );
    }
}

void ProfileStateQuarantine::record_rollback_error(
    platform::linux::config::RollbackResult& result,
    const std::string& operation,
    const fs::path& path,
    const std::string& message
) noexcept {
    result.complete = false;
    result.errors.push_back({operation, path, message});
}

platform::linux::config::RollbackResult ProfileStateQuarantine::rollback() noexcept {
    platform::linux::config::RollbackResult result;
    for (auto iterator = moves_.rbegin(); iterator != moves_.rend(); ++iterator) {
        if (!iterator->moved)
            continue;
        try {
            fs::rename(iterator->destination, iterator->source);
            iterator->moved = false;
            platform::linux::filesystem::fsync_dir(iterator->source.parent_path());
            platform::linux::filesystem::fsync_dir(iterator->destination.parent_path());
        } catch (const std::exception& error) {
            record_rollback_error(result, "restore quarantined profile state", iterator->source, error.what());
        } catch (...) {
            record_rollback_error(result, "restore quarantined profile state", iterator->source, "unknown error");
        }
    }
    remove_empty_transaction_directories();
    return result;
}

void ProfileStateQuarantine::remove_empty_transaction_directories() noexcept {
    std::error_code error;
    fs::remove(transient_transaction_root_, error);
    error.clear();
    fs::remove(transient_transaction_root_.parent_path(), error);
    error.clear();
    fs::remove(roots_.status_root / ".retired", error);
    error.clear();
    fs::remove(retired_transaction_root_, error);
    error.clear();
    fs::remove(retired_profile_root_, error);
    error.clear();
    fs::remove(roots_.state_root / "retired", error);
}

void ProfileStateQuarantine::finish() noexcept {
    if (moves_[2].moved) {
        std::error_code error;
        fs::remove_all(transient_transaction_root_, error);
        moves_[2].moved = false;
    }
    remove_empty_transaction_directories();
}

} // namespace btrfsbackup::daemon::control
