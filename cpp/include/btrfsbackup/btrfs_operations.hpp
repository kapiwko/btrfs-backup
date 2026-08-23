#pragma once

#include <filesystem>
#include <optional>

#include <btrfsbackup/snapshot_inventory.hpp>

namespace btrfsbackup {

class IBtrfsOperations {
public:
    virtual ~IBtrfsOperations() = default;
    virtual bool is_subvolume(const std::filesystem::path& path) = 0;
    virtual std::optional<SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path& path) = 0;
    virtual void create_readonly_snapshot(const std::filesystem::path& source, const std::filesystem::path& target) = 0;
    virtual void delete_subvolume(const std::filesystem::path& path) = 0;
};

class LibBtrfsOperations final : public IBtrfsOperations {
public:
    bool is_subvolume(const std::filesystem::path& path) override;
    std::optional<SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path& path) override;
    void create_readonly_snapshot(const std::filesystem::path& source, const std::filesystem::path& target) override;
    void delete_subvolume(const std::filesystem::path& path) override;
};

} // namespace btrfsbackup
