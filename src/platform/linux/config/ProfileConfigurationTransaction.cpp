// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileConfigurationTransaction.hpp>

#include <exception>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <platform/linux/FileIo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

namespace {

fs::path transaction_path(const fs::path& destination, std::string_view kind, const std::string& generation) {
    return destination.parent_path() / ("." + destination.filename().string() + "." + std::string(kind) + "-" + generation);
}

void remove_if_present(const fs::path& path) noexcept {
    std::error_code error;
    fs::remove(path, error);
}

void record_rollback_error(
    RollbackResult& result,
    std::string_view operation,
    const fs::path& path,
    std::string_view message,
    const fs::path* destination = nullptr
) noexcept {
    result.complete = false;
    try {
        std::string detail{message};
        if (destination != nullptr) {
            detail += "; destination: " + destination->string();
        }
        result.errors.push_back({std::string(operation), path, std::move(detail)});
    } catch (...) {
    }
}

void record_rollback_error(
    RollbackResult& result,
    std::string_view operation,
    const fs::path& path,
    const std::error_code& error,
    const fs::path* destination = nullptr
) noexcept {
    record_rollback_error(result, operation, path, error.message(), destination);
}

bool remove_for_rollback(const fs::path& path, RollbackResult& result, std::string_view operation) noexcept {
    std::error_code error;
    const bool removed = fs::remove(path, error);
    if (error) {
        record_rollback_error(result, operation, path, error);
        return false;
    }
    if (!removed) {
        record_rollback_error(result, operation, path, "path does not exist");
        return false;
    }
    return true;
}

bool rename_for_rollback(
    const fs::path& from,
    const fs::path& to,
    RollbackResult& result,
    std::string_view operation
) noexcept {
    std::error_code error;
    fs::rename(from, to, error);
    if (error) {
        record_rollback_error(result, operation, from, error, &to);
        return false;
    }
    return true;
}

void remove_cleanup_for_rollback(
    const fs::path& path,
    RollbackResult& result,
    std::string_view operation
) noexcept {
    std::error_code error;
    fs::remove(path, error);
    if (error) {
        record_rollback_error(result, operation, path, error);
    }
}

std::string configuration_save_message(const std::string& cause, const RollbackResult& rollback) {
    std::ostringstream message;
    message << "configuration.save_failed: " << cause;
    if (!rollback.complete) {
        message << "; configuration.rollback_incomplete";
        for (const RollbackError& error : rollback.errors) {
            message << "; " << error.operation << " " << error.path.string() << ": " << error.message;
        }
        if (rollback.errors.empty()) {
            message << "; rollback diagnostics could not be recorded";
        }
    }
    return message.str();
}

void rename_checked(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::rename(from, to, error);
    if (error) {
        throw ValidationError("cannot rename " + from.string() + " to " + to.string() + ": " + error.message());
    }
}

void validate_destination(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return;
    }
    if (error) {
        throw ValidationError("cannot inspect configuration destination " + path.string() + ": " + error.message());
    }
    if (fs::is_symlink(status) || !fs::is_regular_file(status)) {
        throw ValidationError("configuration destination is not a regular file: " + path.string());
    }
}

} // namespace

ConfigurationSaveError::ConfigurationSaveError(std::string message, RollbackResult rollback)
    : CodedValidationError(
          rollback.complete ? ErrorCode::ConfigurationSaveFailed : ErrorCode::ConfigurationRollbackIncomplete,
          configuration_save_message(message, rollback)
      ),
      rollback_result(std::move(rollback)) {
}

ProfileConfigurationTransaction::ProfileConfigurationTransaction(const btrfsbackup::config::RenderedProfileArtifacts& rendered)
    : generation_(rendered.profile.configuration_generation) {
    artifacts_.reserve(rendered.artifacts.size());
    for (const btrfsbackup::config::ProfileArtifact& artifact : rendered.artifacts) {
        artifacts_.push_back({
            .kind = artifact.kind,
            .destination = artifact.destination,
            .staged = {},
            .previous = {},
            .content = artifact.content,
            .permissions = artifact.permissions,
            .operation = artifact.operation,
        });
    }
}

void ProfileConfigurationTransaction::stage() {
    for (TransactionArtifact& item : artifacts_) {
        validate_destination(item.destination);
        item.staged = transaction_path(item.destination, "stage", generation_.value());
        item.previous = transaction_path(item.destination, "previous", generation_.value());
        remove_if_present(item.staged);
        remove_if_present(item.previous);
        if (item.operation == btrfsbackup::config::ProfileArtifactOperation::Write) {
            atomic_write(item.staged, item.content, static_cast<mode_t>(item.permissions));
        }
    }
}

fs::path ProfileConfigurationTransaction::staged_path(btrfsbackup::config::ProfileArtifactKind kind) const {
    return artifact(kind).staged;
}

void ProfileConfigurationTransaction::publish_configuration() {
    for (TransactionArtifact& item : artifacts_) {
        if (item.kind != btrfsbackup::config::ProfileArtifactKind::PublicProfile) {
            publish(item);
        }
    }
}

void ProfileConfigurationTransaction::publish_public_marker() {
    publish(artifact(btrfsbackup::config::ProfileArtifactKind::PublicProfile));
}

RollbackResult ProfileConfigurationTransaction::rollback() noexcept {
    RollbackResult result;
    for (auto it = artifacts_.rbegin(); it != artifacts_.rend(); ++it) {
        rollback_artifact(*it, result);
    }
    return result;
}

void ProfileConfigurationTransaction::finish() noexcept {
    for (TransactionArtifact& item : artifacts_) {
        remove_if_present(item.staged);
        remove_if_present(item.previous);
    }
}

void ProfileConfigurationTransaction::publish(TransactionArtifact& item) {
    validate_destination(item.destination);
    std::error_code error;
    item.had_previous = fs::exists(item.destination, error);
    if (error) {
        throw ValidationError("cannot inspect configuration destination " + item.destination.string());
    }
    if (item.had_previous) {
        rename_checked(item.destination, item.previous);
    }
    if (item.operation == btrfsbackup::config::ProfileArtifactOperation::Remove) {
        item.published = item.had_previous;
        fsync_dir(item.destination.parent_path());
        return;
    }
    try {
        rename_checked(item.staged, item.destination);
        item.published = true;
        fsync_dir(item.destination.parent_path());
    } catch (...) {
        if (item.had_previous) {
            std::error_code restore_error;
            fs::rename(item.previous, item.destination, restore_error);
        }
        throw;
    }
}

void ProfileConfigurationTransaction::rollback_artifact(
    TransactionArtifact& item,
    RollbackResult& result
) noexcept {
    remove_published_artifact(item, result);
    const bool restored_previous = restore_previous_artifact(item, result);
    sync_rollback_directory(item, result);
    remove_transaction_files(item, restored_previous, result);
}

void ProfileConfigurationTransaction::remove_published_artifact(
    TransactionArtifact& item,
    RollbackResult& result
) noexcept {
    if (item.published && item.operation == btrfsbackup::config::ProfileArtifactOperation::Write) {
        remove_for_rollback(item.destination, result, "remove published artifact");
    }
}

bool ProfileConfigurationTransaction::restore_previous_artifact(
    TransactionArtifact& item,
    RollbackResult& result
) noexcept {
    if (!item.had_previous) {
        return false;
    }
    return rename_for_rollback(
        item.previous,
        item.destination,
        result,
        "restore previous artifact"
    );
}

void ProfileConfigurationTransaction::sync_rollback_directory(
    const TransactionArtifact& item,
    RollbackResult& result
) noexcept {
    try {
        fsync_dir(item.destination.parent_path());
    } catch (const std::exception& error) {
        record_rollback_error(result, "fsync artifact directory", item.destination.parent_path(), error.what());
    } catch (...) {
        record_rollback_error(result, "fsync artifact directory", item.destination.parent_path(), "unknown error");
    }
}

void ProfileConfigurationTransaction::remove_transaction_files(
    const TransactionArtifact& item,
    bool restored_previous,
    RollbackResult& result
) noexcept {
    remove_cleanup_for_rollback(item.staged, result, "remove staged artifact");
    if (!item.had_previous || restored_previous) {
        remove_cleanup_for_rollback(item.previous, result, "remove rollback artifact");
    }
}

ProfileConfigurationTransaction::TransactionArtifact& ProfileConfigurationTransaction::artifact(
    btrfsbackup::config::ProfileArtifactKind kind
) {
    for (TransactionArtifact& item : artifacts_) {
        if (item.kind == kind) {
            return item;
        }
    }
    throw ValidationError("required profile artifact is missing");
}

const ProfileConfigurationTransaction::TransactionArtifact& ProfileConfigurationTransaction::artifact(
    btrfsbackup::config::ProfileArtifactKind kind
) const {
    for (const TransactionArtifact& item : artifacts_) {
        if (item.kind == kind) {
            return item;
        }
    }
    throw ValidationError("required profile artifact is missing");
}

} // namespace btrfsbackup::platform::linux
