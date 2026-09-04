// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/RealBtrfsTestEnvironment.hpp"
#include "support/RealProvisioningTestEnvironment.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

using btrfsbackup::integration::CommandResult;
using btrfsbackup::integration::RealBtrfsTestEnvironment;
using btrfsbackup::integration::RealProvisioningTestEnvironment;

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void require_backup(const CommandResult& result, bool expected_incremental) {
    require(result.status == 0, "backup failed: " + result.output + result.error_output);
    const auto response = nlohmann::json::parse(result.output);
    require(response.at("schemaVersion") == 1, "backup response has an unexpected schema");
    require(response.at("completed") == true, "backup response is not completed");
    require(response.at("sources").size() == 1, "backup response has an unexpected source count");
    require(
        response.at("sources").at(0).at("incremental") == expected_incremental,
        "backup response did not report the expected transfer kind"
    );
}

void run_scenarios(
    const std::filesystem::path& backupctl,
    const std::filesystem::path& browse_session_client,
    const std::filesystem::path& provisioning_client
) {
    RealBtrfsTestEnvironment environment(backupctl, browse_session_client);
    try {
        environment.prepare();
        std::cout << "artifacts - " << environment.artifact_report() << '\n';

        RealProvisioningTestEnvironment provisioning(provisioning_client, environment.source_subvolume());
        provisioning.require_existing_partition_preserves_sibling();
        std::cout << "ok - partition provisioning preserves its sibling and partition table\n";
        provisioning.require_unallocated_space_preserves_partition();
        std::cout << "ok - free-space provisioning preserves the existing partition\n";
        provisioning.require_whole_device_is_replaced();
        std::cout << "ok - whole-device provisioning creates GPT and an encrypted partition\n";
        provisioning.require_existing_target_is_adopted();
        std::cout << "ok - existing-target adoption preserves bytes and identities\n";
        provisioning.close();

        require_backup(
            environment.execute_backup("2026-08-20T08:00:00Z", "20260820T080000Z-raii-full"),
            false
        );
        require(environment.local_snapshot_count() == 1, "full backup did not create one local snapshot");
        require(environment.remote_snapshot_count() == 1, "full backup did not commit one remote snapshot");
        environment.require_latest_snapshots_match();
        std::cout << "ok - full backup commits a verified real Btrfs snapshot\n";

        environment.write_source_file("file-a.txt", "alpha\nbeta\n");
        environment.write_source_file("dir/file-c.txt", "second generation\n");
        require_backup(
            environment.execute_backup("2026-08-21T08:00:00Z", "20260821T080000Z-raii-incremental"),
            true
        );
        require(environment.local_snapshot_count() == 2, "incremental backup did not retain two local snapshots");
        require(environment.remote_snapshot_count() == 2, "incremental backup did not retain two remote snapshots");
        environment.require_latest_snapshots_match();
        std::cout << "ok - incremental backup uses and verifies its received parent\n";

        environment.create_interrupted_receive_artifact();
        environment.write_source_file("file-d.txt", "third generation\n");
        require_backup(
            environment.execute_backup("2026-08-22T08:00:00Z", "20260822T080000Z-raii-retention"),
            true
        );
        require(environment.incoming_is_empty(), "stale interrupted receive artifact survived cleanup");
        std::cout << "ok - interrupted receive artifacts stay outside the committed repository and are cleaned\n";

        require(environment.local_snapshot_count() == 2, "local retention did not keep exactly two snapshots");
        require(environment.remote_snapshot_count() == 2, "remote retention did not keep exactly two snapshots");
        environment.require_latest_snapshots_match();
        std::cout << "ok - retention keeps the latest two local and remote snapshots\n";

        environment.require_target_identity_rejected();
        std::cout << "ok - target identity mismatch is rejected without snapshot changes\n";

        environment.require_pre_receive_recovery();
        std::cout << "ok - pending recovery removes a pre-receive orphan\n";

        environment.require_post_commit_recovery();
        std::cout << "ok - pending recovery preserves a committed snapshot pair\n";

        environment.require_restore_scenarios();
        std::cout << "ok - raw and public restore paths preserve data and clean staging\n";

        environment.require_public_cancellation();
        std::cout << "ok - public cancellation records terminal state and cleans its request\n";

        environment.require_browse_session();
        std::cout << "ok - unprivileged browse session is read-only and disconnect-cleaned\n";

        environment.close();
    } catch (...) {
        const auto failure = std::current_exception();
        std::cerr << "failure artifacts - " << environment.artifact_report() << '\n';
        try {
            environment.close();
        } catch (const std::exception& cleanup_error) {
            std::cerr << "cleanup after failure: " << cleanup_error.what() << '\n';
        }
        std::rethrow_exception(failure);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: btrfsbackup-real-btrfs-tests /path/to/btrfs-backupctl "
                     "/path/to/browse-session-client /path/to/device-provisioning-client\n";
        return 2;
    }
    try {
        run_scenarios(argv[1], argv[2], argv[3]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "real-btrfs-backup-tests: " << error.what() << '\n';
        return 1;
    }
}
