// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/persistence/FileTargetStorageMeasurementStore.hpp>

#include <cstdint>
#include <string>
#include <utility>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <core/RuntimeTime.hpp>

namespace fs = std::filesystem;
namespace json = btrfsbackup::config::json;

namespace btrfsbackup::state {

namespace {

constexpr int target_storage_document_schema_version = 1;
constexpr std::size_t maximum_document_size = 32 * 1024;
constexpr fs::perms private_directory_permissions = fs::perms::owner_all;
constexpr fs::perms private_file_permissions = fs::perms::owner_read | fs::perms::owner_write;

std::uint64_t non_negative_integer(const json::Json& object, const char* field) {
    const auto item = object.find(field);
    if (item == object.end() || (!item->is_number_integer() && !item->is_number_unsigned())) {
        throw ValidationError(std::string("target storage field must be an integer: ") + field);
    }
    if (item->is_number_unsigned()) {
        return item->get<std::uint64_t>();
    }
    const std::int64_t value = item->get<std::int64_t>();
    if (value < 0) {
        throw ValidationError(std::string("target storage field must be non-negative: ") + field);
    }
    return static_cast<std::uint64_t>(value);
}

std::string string_field(const json::Json& object, const char* field) {
    const auto item = object.find(field);
    if (item == object.end() || !item->is_string()) {
        throw ValidationError(std::string("target storage field must be a string: ") + field);
    }
    return item->get<std::string>();
}

} // namespace

FileTargetStorageMeasurementStore::FileTargetStorageMeasurementStore(
    fs::path state_root,
    IAtomicDocumentWriter* files
)
    : state_root_(std::move(state_root)), files_(files) {
}

fs::path FileTargetStorageMeasurementStore::document_path(const ProfileId& profile_id) const {
    const std::string profile_id_text(profile_id.value());
    validate_profile_id(profile_id_text);
    return state_root_ / "profiles" / profile_id_text / "target-storage.json";
}

void FileTargetStorageMeasurementStore::write(
    const btrfsbackup::config::Profile& profile,
    const btrfsbackup::backup::TargetStorageMeasurement& measurement
) {
    if (files_ == nullptr) {
        throw ValidationError("target storage measurement store is read-only");
    }
    if (!measurement.space.valid()) {
        throw ValidationError("cannot persist invalid target storage measurement");
    }
    const fs::path path = document_path(profile.id);
    files_->ensure_directory(path.parent_path(), private_directory_permissions);
    files_->write_atomically(
        path,
        json::dump_json({
            {"schemaVersion", target_storage_document_schema_version},
            {"profileId", profile.id.value()},
            {"targetIdentity", {
                                   {"luksUuid", profile.target.luks_uuid.value()},
                                   {"btrfsUuid", profile.target.btrfs_uuid.value()},
                                   {"partitionUuid", profile.target.partition_uuid.value()},
                               }},
            {"measurement", {
                                {"capacityBytes", measurement.space.capacity_bytes},
                                {"freeBytes", measurement.space.free_bytes},
                                {"availableBytes", measurement.space.available_bytes},
                                {"measuredAt", format_utc_iso_timestamp(measurement.measured_at)},
                            }},
        }),
        private_file_permissions
    );
}

std::optional<btrfsbackup::backup::TargetStorageMeasurement> FileTargetStorageMeasurementStore::read_matching(
    const btrfsbackup::config::Profile& profile
) const {
    try {
        const json::Json document = json::Json::parse(reader_.read(document_path(profile.id), maximum_document_size));
        if (!document.is_object() || document.value("schemaVersion", -1) != target_storage_document_schema_version ||
            string_field(document, "profileId") != profile.id.value()) {
            return std::nullopt;
        }
        const json::Json& identity = document.at("targetIdentity");
        if (!identity.is_object() || string_field(identity, "luksUuid") != profile.target.luks_uuid.value() ||
            string_field(identity, "btrfsUuid") != profile.target.btrfs_uuid.value() ||
            string_field(identity, "partitionUuid") != profile.target.partition_uuid.value()) {
            return std::nullopt;
        }
        const json::Json& input = document.at("measurement");
        if (!input.is_object()) {
            return std::nullopt;
        }
        const std::optional<RuntimeTimePoint> measured_at = parse_utc_timestamp(string_field(input, "measuredAt"));
        if (!measured_at.has_value()) {
            return std::nullopt;
        }
        btrfsbackup::backup::FilesystemSpace space{
            .capacity_bytes = non_negative_integer(input, "capacityBytes"),
            .free_bytes = non_negative_integer(input, "freeBytes"),
            .available_bytes = non_negative_integer(input, "availableBytes"),
        };
        if (!space.valid()) {
            return std::nullopt;
        }
        return btrfsbackup::backup::TargetStorageMeasurement{
            .space = space,
            .measured_at = *measured_at,
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace btrfsbackup::state
