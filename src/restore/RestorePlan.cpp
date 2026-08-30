// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <restore/RestorePlan.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <restore/RestoreError.hpp>

namespace btrfsbackup::restore {

namespace {

bool valid_transaction_id(const std::string& value) {
    return !value.empty() && value.size() <= 64 && std::ranges::all_of(value, [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

void validate_destination(const std::filesystem::path& destination) {
    if (!destination.is_absolute() || destination.lexically_normal() != destination || destination == "/" ||
        destination == "/home") {
        throw RestoreError(RestoreErrorCode::DestinationUnsafe, "restore destination must be a canonical absolute path outside / and /home");
    }
    std::filesystem::path current;
    for (const std::filesystem::path& component : destination.parent_path()) {
        current /= component;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(current, error);
        if (!error && std::filesystem::is_symlink(status)) {
            throw RestoreError(RestoreErrorCode::SymlinkRejected, "restore destination traverses a symbolic link: " + current.string());
        }
        if (error && error != std::errc::no_such_file_or_directory) {
            throw RestoreError(RestoreErrorCode::DestinationUnsafe, "restore destination cannot be inspected: " + current.string());
        }
    }
}

void validate_source(const std::filesystem::path& source) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(source, error);
    if (error || (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status))) {
        throw RestoreError(RestoreErrorCode::PathInvalid, "restore source is not a regular file or directory");
    }
    if (std::filesystem::is_symlink(status)) {
        throw RestoreError(RestoreErrorCode::SymlinkRejected, "restore source is a symbolic link");
    }
}

} // namespace

RestorePlan RestorePlanner::plan(const RepositoryCatalog& catalog, const RestoreRequest& request) const {
    if (!valid_transaction_id(request.transaction_id)) {
        throw RestoreError(RestoreErrorCode::PathInvalid, "restore transaction id is invalid");
    }
    validate_destination(request.destination);
    const CatalogSnapshot& snapshot = catalog.snapshot(request.snapshot_id);
    const std::filesystem::path source = catalog.root() / snapshot.repository_path.value() / request.source_path.value();
    validate_source(source);

    std::error_code exists_error;
    const bool destination_exists = std::filesystem::exists(std::filesystem::symlink_status(request.destination, exists_error));
    if (exists_error && exists_error != std::errc::no_such_file_or_directory) {
        throw RestoreError(RestoreErrorCode::DestinationUnsafe, "restore destination cannot be inspected");
    }
    if (destination_exists && request.existing_destination == ExistingDestinationPolicy::Fail) {
        throw RestoreError(RestoreErrorCode::DestinationExists, "restore destination already exists: " + request.destination.string());
    }

    const std::string stem = ".btrfs-backup-restore-" + request.transaction_id;
    const std::filesystem::path parent = request.destination.parent_path();
    return RestorePlan{
        .transaction_id = request.transaction_id,
        .snapshot_id = request.snapshot_id,
        .snapshot_uuid = snapshot.uuid,
        .source = source,
        .destination = request.destination,
        .staging = parent / (stem + ".staging"),
        .previous = parent / (stem + ".previous"),
        .kind = request.kind,
        .existing_destination = request.existing_destination,
        .destination_exists = destination_exists,
    };
}

} // namespace btrfsbackup::restore
