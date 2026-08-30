// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <restore/RepositoryDiscoveryService.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/RuntimeTime.hpp>
#include <restore/RestoreError.hpp>

namespace btrfsbackup::restore {

namespace {

nlohmann::json read_object(const std::filesystem::path& path, RestoreErrorCode code) {
    std::error_code status_error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, status_error);
    if (status_error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        throw RestoreError(code, "required repository metadata is not a regular file: " + path.string());
    }
    std::ifstream stream(path);
    try {
        nlohmann::json document;
        stream >> document;
        if (!document.is_object()) {
            throw RestoreError(code, "repository metadata root must be an object: " + path.string());
        }
        return document;
    } catch (const nlohmann::json::exception& error) {
        throw RestoreError(code, "invalid repository metadata " + path.string() + ": " + error.what());
    }
}

std::string required_string(const nlohmann::json& object, const char* key, RestoreErrorCode code) {
    if (!object.contains(key) || !object.at(key).is_string() || object.at(key).get<std::string>().empty()) {
        throw RestoreError(code, std::string("missing non-empty string: ") + key);
    }
    return object.at(key).get<std::string>();
}

RuntimeTimePoint required_time(const nlohmann::json& object, const char* key, RestoreErrorCode code) {
    const std::string value = required_string(object, key, code);
    std::optional<RuntimeTimePoint> parsed = parse_utc_timestamp(value);
    if (!parsed.has_value()) {
        throw RestoreError(code, std::string("invalid UTC timestamp: ") + key);
    }
    return *parsed;
}

void reject_symlink_components(const std::filesystem::path& root, const RelativeRestorePath& relative) {
    std::filesystem::path current = root;
    for (const std::filesystem::path& component : relative.value()) {
        current /= component;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(current, error);
        if (error) {
            throw RestoreError(RestoreErrorCode::CatalogInvalid, "catalog path cannot be inspected: " + current.string());
        }
        if (std::filesystem::is_symlink(status)) {
            throw RestoreError(RestoreErrorCode::SymlinkRejected, "catalog path contains a symbolic link: " + current.string());
        }
    }
}

} // namespace

RepositoryDiscoveryService::RepositoryDiscoveryService(DiscoveredSnapshotMetadataReader metadata_reader)
    : metadata_reader_(std::move(metadata_reader)) {
    if (!metadata_reader_) {
        throw RestoreError(RestoreErrorCode::RepositoryMetadataInvalid, "snapshot metadata reader is required");
    }
}

RepositoryCatalog RepositoryDiscoveryService::discover(const std::filesystem::path& already_mounted_root) const {
    std::error_code root_error;
    const std::filesystem::file_status root_status = std::filesystem::symlink_status(already_mounted_root, root_error);
    if (root_error || !std::filesystem::is_directory(root_status) || std::filesystem::is_symlink(root_status)) {
        throw RestoreError(RestoreErrorCode::RepositoryNotFound, "repository root is not a directory: " + already_mounted_root.string());
    }

    const nlohmann::json repository = read_object(
        already_mounted_root / "repository.json",
        RestoreErrorCode::RepositoryMetadataInvalid
    );
    const int repository_version = repository.value("schemaVersion", 0);
    if (repository_version != repository_format_version) {
        throw RestoreError(RestoreErrorCode::RepositoryFormatUnsupported, "unsupported repository format version: " + std::to_string(repository_version));
    }

    RepositoryIdentity identity{
        .repository_id = required_string(repository, "repositoryId", RestoreErrorCode::RepositoryMetadataInvalid),
        .target_filesystem_uuid = required_string(repository, "targetFilesystemUuid", RestoreErrorCode::RepositoryMetadataInvalid),
        .created_at = required_time(repository, "createdAt", RestoreErrorCode::RepositoryMetadataInvalid),
        .features = {},
    };
    if (repository.contains("features")) {
        if (!repository.at("features").is_array()) {
            throw RestoreError(RestoreErrorCode::RepositoryMetadataInvalid, "repository features must be an array");
        }
        for (const nlohmann::json& feature : repository.at("features")) {
            if (!feature.is_string()) {
                throw RestoreError(RestoreErrorCode::RepositoryMetadataInvalid, "repository feature must be a string");
            }
            identity.features.push_back(feature.get<std::string>());
        }
    }

    const nlohmann::json catalog = read_object(already_mounted_root / "catalog.json", RestoreErrorCode::CatalogInvalid);
    if (catalog.value("schemaVersion", 0) != catalog_format_version || !catalog.contains("snapshots") ||
        !catalog.at("snapshots").is_array()) {
        throw RestoreError(RestoreErrorCode::CatalogInvalid, "unsupported or malformed catalog");
    }
    const std::uint64_t generation = catalog.value("generation", std::uint64_t{0});
    std::vector<CatalogSnapshot> snapshots;
    for (const nlohmann::json& value : catalog.at("snapshots")) {
        if (!value.is_object()) {
            throw RestoreError(RestoreErrorCode::CatalogInvalid, "catalog snapshot must be an object");
        }
        CatalogSnapshot entry{
            .snapshot_id = required_string(value, "snapshotId", RestoreErrorCode::CatalogInvalid),
            .host_id = required_string(value, "hostId", RestoreErrorCode::CatalogInvalid),
            .profile_id = required_string(value, "profileId", RestoreErrorCode::CatalogInvalid),
            .source_id = required_string(value, "sourceId", RestoreErrorCode::CatalogInvalid),
            .repository_path = RelativeRestorePath{required_string(value, "relativePath", RestoreErrorCode::CatalogInvalid)},
            .created_at = required_time(value, "createdAt", RestoreErrorCode::CatalogInvalid),
            .uuid = required_string(value, "uuid", RestoreErrorCode::CatalogInvalid),
            .received_uuid = value.value("receivedUuid", ""),
            .parent_uuid = value.value("parentUuid", ""),
            .verified = value.value("verified", false),
        };
        if (entry.repository_path.empty()) {
            throw RestoreError(RestoreErrorCode::CatalogInvalid, "snapshot repository path cannot be the repository root");
        }
        reject_symlink_components(already_mounted_root, entry.repository_path);
        std::optional<DiscoveredSnapshotMetadata> actual = metadata_reader_(already_mounted_root / entry.repository_path.value());
        if (!actual.has_value() || !actual->is_subvolume || !actual->readonly || actual->uuid != entry.uuid ||
            (!entry.received_uuid.empty() && actual->received_uuid != entry.received_uuid)) {
            throw RestoreError(RestoreErrorCode::SnapshotIdentityMismatch, "catalog snapshot identity mismatch: " + entry.snapshot_id);
        }
        snapshots.push_back(std::move(entry));
    }

    return RepositoryCatalog{already_mounted_root, std::move(identity), generation, std::move(snapshots)};
}

} // namespace btrfsbackup::restore
