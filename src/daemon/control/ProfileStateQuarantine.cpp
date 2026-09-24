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
    state_retired_root_ = roots_.state_root / "retired";
    state_profile_root_ = state_retired_root_ / profile_id;
    state_transaction_root_ = state_profile_root_ / transaction;
    history_retired_root_ = roots_.history_root / ".retired";
    history_profile_root_ = history_retired_root_ / profile_id;
    history_transaction_root_ = history_profile_root_ / transaction;
    status_retired_root_ = roots_.status_root / ".retired";
    status_profile_root_ = status_retired_root_ / profile_id;
    status_transaction_root_ = status_profile_root_ / transaction;
    moves_ = {
        {roots_.state_root / "profiles" / profile_id, state_transaction_root_ / "state"},
        {roots_.history_root / profile_id, history_transaction_root_},
        {roots_.status_root / profile_id, status_transaction_root_ / "status"},
    };
}

void ProfileStateQuarantine::create_private_directory(const fs::path& path) {
    std::error_code error;
    bool created = false;
    const fs::file_status status = fs::symlink_status(path, error);
    if (!error && status.type() != fs::file_type::not_found) {
        if (!fs::is_directory(status) || fs::is_symlink(status))
            throw ValidationError("profile state quarantine path is not a directory: " + path.string());
    } else if (error && error != std::errc::no_such_file_or_directory) {
        throw ValidationError("cannot inspect profile state quarantine path: " + path.string());
    } else {
        if (!fs::create_directory(path, error) || error)
            throw ValidationError("cannot create profile state quarantine directory: " + path.string());
        created = true;
    }
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, error);
    if (error)
        throw ValidationError("cannot secure profile state quarantine directory: " + path.string());
    if (created) {
        platform::linux::filesystem::fsync_dir(path);
        platform::linux::filesystem::fsync_dir(path.parent_path());
    }
}

void ProfileStateQuarantine::create_transaction_directories(
    const fs::path& retired_root,
    const fs::path& profile_root,
    const fs::path& transaction_root
) {
    create_private_directory(retired_root);
    create_private_directory(profile_root);
    create_private_directory(transaction_root);
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

        if (persistent_state)
            create_transaction_directories(state_retired_root_, state_profile_root_, state_transaction_root_);
        if (persistent_history) {
            create_private_directory(history_retired_root_);
            create_private_directory(history_profile_root_);
        }
        if (transient_status)
            create_transaction_directories(status_retired_root_, status_profile_root_, status_transaction_root_);
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
    try {
        result.errors.push_back({operation, path, message});
    } catch (...) {
        result.diagnostics_incomplete = true;
    }
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
    fs::remove(status_transaction_root_, error);
    error.clear();
    fs::remove(status_profile_root_, error);
    error.clear();
    fs::remove(status_retired_root_, error);
    error.clear();
    fs::remove(history_transaction_root_, error);
    error.clear();
    fs::remove(history_profile_root_, error);
    error.clear();
    fs::remove(history_retired_root_, error);
    error.clear();
    fs::remove(state_transaction_root_, error);
    error.clear();
    fs::remove(state_profile_root_, error);
    error.clear();
    fs::remove(state_retired_root_, error);
}

ProfileStateQuarantineFinishResult ProfileStateQuarantine::finish() noexcept {
    ProfileStateQuarantineFinishResult result;
    if (moves_[2].moved) {
        std::error_code error;
        fs::remove_all(status_transaction_root_, error);
        result.transient_status_removed = !error;
        if (result.transient_status_removed) {
            try {
                platform::linux::filesystem::fsync_dir(status_transaction_root_.parent_path());
            } catch (...) {
                result.parent_directory_synced = false;
            }
        }
        moves_[2].moved = false;
    }
    remove_empty_transaction_directories();
    return result;
}

} // namespace btrfsbackup::daemon::control
