// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <restore/SnapshotBrowser.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <restore/RestoreError.hpp>

namespace btrfsbackup::restore {

namespace {

std::filesystem::path resolve_without_symlinks(
    const RepositoryCatalog& catalog,
    const CatalogSnapshot& snapshot,
    const RelativeRestorePath& relative_path
) {
    std::filesystem::path current = catalog.root() / snapshot.repository_path.value();
    for (const std::filesystem::path& component : relative_path.value()) {
        current /= component;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(current, error);
        if (error || !std::filesystem::exists(status)) {
            throw RestoreError(RestoreErrorCode::PathInvalid, "snapshot path does not exist: " + relative_path.value().string());
        }
        if (std::filesystem::is_symlink(status)) {
            throw RestoreError(RestoreErrorCode::SymlinkRejected, "snapshot path traverses a symbolic link: " + relative_path.value().string());
        }
    }
    return current;
}

SnapshotEntryType entry_type(const std::filesystem::file_status& status) {
    if (std::filesystem::is_regular_file(status)) return SnapshotEntryType::File;
    if (std::filesystem::is_directory(status)) return SnapshotEntryType::Directory;
    if (std::filesystem::is_symlink(status)) return SnapshotEntryType::Symlink;
    return SnapshotEntryType::Other;
}

} // namespace

std::vector<SnapshotEntry> SnapshotBrowser::list(
    const RepositoryCatalog& catalog,
    const std::string& snapshot_id,
    const RelativeRestorePath& relative_path
) const {
    const std::filesystem::path directory = resolve_without_symlinks(catalog, catalog.snapshot(snapshot_id), relative_path);
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        throw RestoreError(RestoreErrorCode::PathInvalid, "snapshot path is not a directory: " + relative_path.value().string());
    }
    std::vector<SnapshotEntry> entries;
    for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(directory)) {
        const std::filesystem::file_status status = item.symlink_status(error);
        if (error) {
            throw RestoreError(RestoreErrorCode::PathInvalid, "could not inspect snapshot entry: " + item.path().filename().string());
        }
        entries.push_back(SnapshotEntry{
            .name = item.path().filename().string(),
            .type = entry_type(status),
            .size = std::filesystem::is_regular_file(status) ? item.file_size(error) : 0,
        });
    }
    std::ranges::sort(entries, {}, &SnapshotEntry::name);
    return entries;
}

std::filesystem::path SnapshotBrowser::resolve_regular_file(
    const RepositoryCatalog& catalog,
    const std::string& snapshot_id,
    const RelativeRestorePath& relative_path
) const {
    const std::filesystem::path file = resolve_without_symlinks(catalog, catalog.snapshot(snapshot_id), relative_path);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(file, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        throw RestoreError(RestoreErrorCode::PathInvalid, "snapshot path is not a regular file: " + relative_path.value().string());
    }
    return file;
}

} // namespace btrfsbackup::restore
