#include <btrfsbackup/snapshot_transfer.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>

namespace fs = std::filesystem;

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool uuid_equals(const std::string& left, const std::string& right) {
    return !left.empty() && !right.empty() && lowercase(left) == lowercase(right);
}

} // namespace

namespace btrfsbackup {

SendReceiveCommandPlan build_send_receive_command_plan(
    const fs::path& snapshot_path,
    const fs::path& parent_path,
    const fs::path& receive_directory
) {
    SendReceiveCommandPlan plan;
    plan.send_argv = {"btrfs", "send"};
    if (!parent_path.empty()) {
        plan.send_argv.push_back("-p");
        plan.send_argv.push_back(parent_path.string());
    }
    plan.send_argv.push_back(snapshot_path.string());
    plan.receive_argv = {"btrfs", "receive", receive_directory.string()};
    return plan;
}

void verify_received_snapshot(
    const std::string& source_id,
    const SnapshotMetadata& local_snapshot,
    const SnapshotMetadata& received_snapshot
) {
    if (!received_snapshot.is_subvolume) {
        throw ValidationError("btrfs receive did not create the expected subvolume");
    }
    if (!received_snapshot.readonly) {
        throw ValidationError("Received subvolume is not readonly");
    }
    if (!uuid_equals(local_snapshot.uuid, received_snapshot.received_uuid)) {
        throw ValidationError("Received UUID does not match the local snapshot UUID for " + source_id);
    }
}

void commit_received_snapshot(
    IBtrfsOperations& btrfs,
    IFileSystemEffects& fs_effects,
    const fs::path& received_path,
    const fs::path& final_path,
    const std::string& expected_received_uuid
) {
    if (fs_effects.exists(final_path)) {
        throw ValidationError("Destination snapshot already exists: " + final_path.string());
    }

    btrfs.create_readonly_snapshot(received_path, final_path);
    std::optional<SnapshotMetadata> committed = btrfs.read_snapshot_metadata(final_path);
    if (!committed.has_value() || !uuid_equals(committed->received_uuid, expected_received_uuid)) {
        try {
            btrfs.delete_subvolume(final_path);
        } catch (const std::exception&) {
        }
        throw ValidationError("Committed snapshot Received UUID does not match the local snapshot UUID");
    }
}

} // namespace btrfsbackup
