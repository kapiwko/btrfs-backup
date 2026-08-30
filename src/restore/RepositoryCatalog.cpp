// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <restore/RepositoryCatalog.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

#include <restore/RestoreError.hpp>

namespace btrfsbackup::restore {

namespace {

std::filesystem::path validate_relative_path(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) {
        throw RestoreError(RestoreErrorCode::PathInvalid, "restore path is empty or contains NUL");
    }
    const std::filesystem::path path{value};
    if (path == ".") {
        return path;
    }
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        throw RestoreError(RestoreErrorCode::PathTraversal, "restore path must be relative: " + value);
    }
    for (const std::filesystem::path& component : path) {
        if (component == "..") {
            throw RestoreError(RestoreErrorCode::PathTraversal, "restore path contains '..': " + value);
        }
        if (component.empty() || component == ".") {
            throw RestoreError(RestoreErrorCode::PathInvalid, "restore path is not canonical: " + value);
        }
    }
    if (path.lexically_normal() != path) {
        throw RestoreError(RestoreErrorCode::PathInvalid, "restore path is not canonical: " + value);
    }
    return path;
}

} // namespace

RelativeRestorePath::RelativeRestorePath(std::string value)
    : value_(validate_relative_path(value)) {
}

const std::filesystem::path& RelativeRestorePath::value() const noexcept {
    return value_;
}

bool RelativeRestorePath::empty() const noexcept {
    return value_ == ".";
}

RepositoryCatalog::RepositoryCatalog(
    std::filesystem::path root,
    RepositoryIdentity identity,
    std::uint64_t generation,
    std::vector<CatalogSnapshot> snapshots
)
    : root_(std::move(root)),
      identity_(std::move(identity)),
      generation_(generation),
      snapshots_(std::move(snapshots)) {
    std::set<std::string> ids;
    for (const CatalogSnapshot& entry : snapshots_) {
        if (entry.snapshot_id.empty() || entry.host_id.empty() || entry.profile_id.empty() || entry.source_id.empty() ||
            entry.uuid.empty() || !entry.verified) {
            throw RestoreError(RestoreErrorCode::CatalogInvalid, "catalog contains an incomplete or unverified snapshot");
        }
        if (!ids.insert(entry.snapshot_id).second) {
            throw RestoreError(RestoreErrorCode::CatalogInvalid, "duplicate snapshot id: " + entry.snapshot_id);
        }
    }
    std::sort(snapshots_.begin(), snapshots_.end(), [](const CatalogSnapshot& left, const CatalogSnapshot& right) {
        if (left.created_at != right.created_at) {
            return left.created_at > right.created_at;
        }
        return left.snapshot_id < right.snapshot_id;
    });
}

const std::filesystem::path& RepositoryCatalog::root() const noexcept {
    return root_;
}

const RepositoryIdentity& RepositoryCatalog::identity() const noexcept {
    return identity_;
}

std::uint64_t RepositoryCatalog::generation() const noexcept {
    return generation_;
}

const std::vector<CatalogSnapshot>& RepositoryCatalog::snapshots() const noexcept {
    return snapshots_;
}

const CatalogSnapshot& RepositoryCatalog::snapshot(const std::string& snapshot_id) const {
    auto found = std::ranges::find(snapshots_, snapshot_id, &CatalogSnapshot::snapshot_id);
    if (found == snapshots_.end()) {
        throw RestoreError(RestoreErrorCode::SnapshotNotFound, "snapshot not found: " + snapshot_id);
    }
    return *found;
}

std::vector<const CatalogSnapshot*> find_versions(
    const RepositoryCatalog& catalog,
    const std::string& host_id,
    const std::string& profile_id,
    const std::string& source_id,
    const RelativeRestorePath& relative_path
) {
    std::vector<const CatalogSnapshot*> versions;
    for (const CatalogSnapshot& snapshot : catalog.snapshots()) {
        if (snapshot.host_id != host_id || snapshot.profile_id != profile_id || snapshot.source_id != source_id) {
            continue;
        }
        std::error_code error;
        std::filesystem::path candidate = catalog.root() / snapshot.repository_path.value();
        for (const std::filesystem::path& component : relative_path.value()) {
            candidate /= component;
            const std::filesystem::file_status component_status = std::filesystem::symlink_status(candidate, error);
            if (!error && std::filesystem::is_symlink(component_status)) {
                throw RestoreError(RestoreErrorCode::SymlinkRejected, "version path traverses a symbolic link");
            }
            if (error || !std::filesystem::exists(component_status)) {
                break;
            }
        }
        const std::filesystem::file_status status = std::filesystem::symlink_status(candidate, error);
        if (!error && std::filesystem::exists(status)) {
            versions.push_back(&snapshot);
        }
    }
    return versions;
}

} // namespace btrfsbackup::restore
