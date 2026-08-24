#include <btrfsbackup/snapshot_inventory.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <cstdio>
#include <string>
#include <vector>

#include <btrfsutil.h>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>

namespace fs = std::filesystem;

namespace {

bool all_digits(const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool valid_snapshot_timestamp(const std::string& value) {
    return value.size() == 18
        && all_digits(value.substr(0, 4))
        && value[4] == '-'
        && all_digits(value.substr(5, 2))
        && value[7] == '-'
        && all_digits(value.substr(8, 2))
        && value[10] == 'T'
        && all_digits(value.substr(11, 6))
        && value[17] == 'Z';
}

bool is_zero_uuid(const uint8_t uuid[16]) {
    for (int index = 0; index < 16; ++index) {
        if (uuid[index] != 0) {
            return false;
        }
    }
    return true;
}

std::string uuid_to_string(const uint8_t uuid[16]) {
    std::array<char, 37> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0],
        uuid[1],
        uuid[2],
        uuid[3],
        uuid[4],
        uuid[5],
        uuid[6],
        uuid[7],
        uuid[8],
        uuid[9],
        uuid[10],
        uuid[11],
        uuid[12],
        uuid[13],
        uuid[14],
        uuid[15]
    );
    return buffer.data();
}

bool metadata_sort_key_less(const btrfsbackup::SnapshotInfo& left, const btrfsbackup::SnapshotInfo& right) {
    if (left.timestamp != right.timestamp) {
        return left.timestamp < right.timestamp;
    }
    if (left.sequence != right.sequence) {
        return left.sequence < right.sequence;
    }
    return left.path.string() < right.path.string();
}

} // namespace

namespace btrfsbackup {

std::optional<SnapshotName> parse_snapshot_name(const std::string& name, const std::string& source_id) {
    validate_identifier(source_id, "sourceId");
    const std::string prefix = source_id + "-";
    if (name.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    const std::string rest = name.substr(prefix.size());
    if (rest.size() != 18 && rest.size() != 21) {
        return std::nullopt;
    }

    const std::string timestamp = rest.substr(0, 18);
    if (!valid_snapshot_timestamp(timestamp)) {
        return std::nullopt;
    }

    int sequence = 0;
    if (rest.size() == 21) {
        if (rest[18] != '-' || !all_digits(rest.substr(19, 2))) {
            return std::nullopt;
        }
        sequence = std::stoi(rest.substr(19, 2));
        if (sequence <= 0) {
            return std::nullopt;
        }
    }

    return SnapshotName{
        .source_id = source_id,
        .timestamp = timestamp,
        .sequence = sequence,
        .name = name,
    };
}

std::vector<SnapshotInfo> list_snapshot_inventory(
    const fs::path& directory,
    const std::string& source_id,
    SnapshotSide side,
    const SnapshotMetadataReader& metadata_reader
) {
    return list_snapshot_inventory_at(directory, directory, source_id, side, metadata_reader);
}

std::vector<SnapshotInfo> list_snapshot_inventory_at(
    const fs::path& scan_directory,
    const fs::path& reported_directory,
    const std::string& source_id,
    SnapshotSide side,
    const SnapshotMetadataReader& metadata_reader
) {
    validate_identifier(source_id, "sourceId");
    if (!metadata_reader) {
        throw ValidationError("snapshot metadata reader is required");
    }

    std::vector<SnapshotInfo> snapshots;
    if (!fs::exists(scan_directory)) {
        return snapshots;
    }
    if (!fs::is_directory(scan_directory)) {
        throw ValidationError("snapshot inventory path is not a directory: " + reported_directory.string());
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(scan_directory)) {
        std::error_code status_error;
        fs::file_status status = entry.symlink_status(status_error);
        if (status_error) {
            throw ValidationError("could not inspect snapshot inventory entry: " + entry.path().string());
        }
        if (fs::is_symlink(status)) {
            throw ValidationError("symbolic link is forbidden in snapshot inventory: " + (reported_directory / entry.path().filename()).string());
        }
        if (!fs::is_directory(status)) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        std::optional<SnapshotName> parsed = parse_snapshot_name(name, source_id);
        if (!parsed.has_value()) {
            continue;
        }

        std::optional<SnapshotMetadata> metadata = metadata_reader(entry.path());
        if (!metadata.has_value() || !metadata->is_subvolume) {
            continue;
        }

        snapshots.push_back(SnapshotInfo{
            .side = side,
            .source_id = parsed->source_id,
            .name = parsed->name,
            .timestamp = parsed->timestamp,
            .sequence = parsed->sequence,
            .path = reported_directory / entry.path().filename(),
            .readonly = metadata->readonly,
            .uuid = metadata->uuid,
            .received_uuid = metadata->received_uuid,
        });
    }

    std::sort(snapshots.begin(), snapshots.end(), metadata_sort_key_less);
    return snapshots;
}

std::optional<SnapshotMetadata> read_btrfs_snapshot_metadata(const fs::path& path) {
    struct btrfs_util_subvolume_info info{};
    enum btrfs_util_error info_error = btrfs_util_subvolume_get_info(path.c_str(), 0, &info);
    if (info_error == BTRFS_UTIL_ERROR_NOT_BTRFS
        || info_error == BTRFS_UTIL_ERROR_NOT_SUBVOLUME
        || info_error == BTRFS_UTIL_ERROR_SUBVOLUME_NOT_FOUND) {
        return std::nullopt;
    }
    if (info_error != BTRFS_UTIL_OK) {
        throw ValidationError("could not read Btrfs subvolume info for " + path.string() + ": " + btrfs_util_strerror(info_error));
    }

    SnapshotMetadata metadata;
    metadata.is_subvolume = true;
    metadata.uuid = uuid_to_string(info.uuid);
    if (!is_zero_uuid(info.received_uuid)) {
        metadata.received_uuid = uuid_to_string(info.received_uuid);
    }

    bool readonly = false;
    enum btrfs_util_error readonly_error = btrfs_util_subvolume_get_read_only(path.c_str(), &readonly);
    if (readonly_error != BTRFS_UTIL_OK) {
        throw ValidationError("could not read Btrfs subvolume readonly flag for " + path.string() + ": " + btrfs_util_strerror(readonly_error));
    }
    metadata.readonly = readonly;

    return metadata;
}

} // namespace btrfsbackup
