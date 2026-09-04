// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealBtrfsTestEnvironment.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

[[nodiscard]] std::string read_document(const fs::path& path) {
    std::ifstream input(path);
    const std::string result{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input.eof())
        throw std::runtime_error("cannot read " + path.string());
    return result;
}

} // namespace

void RealBtrfsTestEnvironment::require_source_on_target_rejected() const {
    const fs::path profile_path = config_root_ / "profiles/raii/profile.json";
    const std::string original_profile = read_document(profile_path);
    Json profile = Json::parse(original_profile);
    const fs::path bad_source = target_mount_ / "bad-source";
    const fs::path bad_local = target_mount_ / ".bad-local";
    require_command({"btrfs", "subvolume", "create", bad_source.string()}, "create target-hosted source");
    fs::create_directory(bad_local);
    profile["sources"][0]["subvolume"] = bad_source.string();
    profile["sources"][0]["localSnapshotDir"] = bad_local.string();
    write_test_file(profile_path, profile.dump() + "\n");

    CommandResult result;
    try {
        result = execute_backup("2026-08-25T08:00:00Z", "20260825T080000Z-raii-source-on-target");
        write_test_file(profile_path, original_profile);
        require_command({"btrfs", "subvolume", "delete", bad_source.string()}, "delete target-hosted source");
        fs::remove(bad_local);
    } catch (...) {
        write_test_file(profile_path, original_profile);
        std::error_code ignored;
        fs::remove(bad_local, ignored);
        static_cast<void>(command({"btrfs", "subvolume", "delete", bad_source.string()}));
        throw;
    }
    if (result.status == 0 ||
        !command_diagnostic(result).contains("SOURCE_SUBVOLUME must not be on the backup target filesystem"))
        throw std::runtime_error("runner did not reject a source on its backup target: " + command_diagnostic(result));
}

void RealBtrfsTestEnvironment::require_incoming_symlink_rejected() const {
    const fs::path incoming = target_mount_ / ".incoming/home";
    const fs::path outside = root_ / "incoming-escape-target";
    fs::create_directory(outside);
    write_test_file(outside / "sentinel", "keep\n");
    if (fs::exists(incoming) && !fs::remove(incoming))
        throw std::runtime_error("incoming directory is not empty before symlink scenario");
    fs::create_symlink(outside, incoming);

    CommandResult result;
    try {
        result = execute_backup("2026-08-25T09:00:00Z", "20260825T090000Z-raii-incoming-symlink");
        fs::remove(incoming);
        fs::create_directory(incoming);
    } catch (...) {
        std::error_code ignored;
        fs::remove(incoming, ignored);
        fs::create_directory(incoming, ignored);
        throw;
    }
    if (result.status == 0 || !command_diagnostic(result).contains("Too many levels of symbolic links"))
        throw std::runtime_error("runner did not reject an incoming symlink: " + command_diagnostic(result));
    if (read_document(outside / "sentinel") != "keep\n")
        throw std::runtime_error("incoming symlink handling modified data outside the repository");
    fs::remove_all(outside);
}

void RealBtrfsTestEnvironment::require_missing_incremental_parent_rejected() const {
    const fs::path profile_path = config_root_ / "profiles/raii/profile.json";
    const std::string original_profile = read_document(profile_path);
    Json profile = Json::parse(original_profile);
    const fs::path empty_local = source_mount_ / ".snapshots/empty-parent-check";
    fs::create_directory(empty_local);
    profile["sources"][0]["localSnapshotDir"] = empty_local.string();
    write_test_file(profile_path, profile.dump() + "\n");
    write_source_file("orphan-parent-check.txt", "delta\n");

    CommandResult result;
    try {
        result = execute_backup("2026-08-25T10:00:00Z", "20260825T100000Z-raii-missing-parent");
        write_test_file(profile_path, original_profile);
    } catch (...) {
        write_test_file(profile_path, original_profile);
        throw;
    }
    for (const auto& entry : fs::directory_iterator(empty_local))
        require_command({"btrfs", "subvolume", "delete", entry.path().string()}, "delete rejected local snapshot");
    fs::remove(empty_local);
    if (result.status == 0 ||
        !command_diagnostic(result).contains("Remote snapshots exist for home, but no UUID-matching local parent was found."))
        throw std::runtime_error("runner accepted a missing incremental parent: " + command_diagnostic(result));
}

} // namespace btrfsbackup::integration
