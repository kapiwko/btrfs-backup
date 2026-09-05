// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include <core/Cancellation.hpp>
#include <platform/linux/restore/PosixRestoreOperations.hpp>
#include <restore/RestoreEngine.hpp>
#include <restore/RestoreError.hpp>
#include <restore/RestorePlan.hpp>
#include <support/TestHelpers.hpp>

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

btrfsbackup::restore::RepositoryCatalog catalog(const fs::path& root) {
    return btrfsbackup::restore::RepositoryCatalog{
        root,
        btrfsbackup::restore::RepositoryIdentity{
            .repository_id = "repository",
            .target_filesystem_uuid = "target",
            .created_at = test_helpers::runtime_time("2026-08-30T120000Z"),
            .features = {},
        },
        1,
        {btrfsbackup::restore::CatalogSnapshot{
            .snapshot_id = "snapshot",
            .host_id = "host",
            .profile_id = "profile",
            .source_id = "home",
            .repository_path = btrfsbackup::restore::RelativeRestorePath{"snapshot"},
            .created_at = test_helpers::runtime_time("2026-08-30T120000Z"),
            .uuid = "uuid",
            .received_uuid = "received",
            .parent_uuid = "",
            .verified = true,
        }}
    };
}

btrfsbackup::restore::RestoreResult execute(
    const btrfsbackup::restore::RepositoryCatalog& repository,
    const btrfsbackup::restore::RestoreRequest& request
) {
    btrfsbackup::restore::RestorePlanner planner;
    const btrfsbackup::restore::RestorePlan plan = planner.plan(repository, request);
    btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    btrfsbackup::CancellationToken cancellation;
    return executor.execute(plan, cancellation);
}

void test_restores_directory_transactionally() {
    const fs::path root = test_helpers::test_root("restore-engine", "directory");
    test_helpers::write_file(root / "repository/snapshot/Documents/report.txt", "restored");
    const auto repository = catalog(root / "repository");
    const fs::path destination = root / "output/Documents";
    const auto result = execute(repository, btrfsbackup::restore::RestoreRequest{
                                                .transaction_id = "tx-directory",
                                                .snapshot_id = "snapshot",
                                                .source_path = btrfsbackup::restore::RelativeRestorePath{"Documents"},
                                                .destination = destination,
                                            });
    test_helpers::expect_true("directory committed", result.committed, "restore should commit");
    test_helpers::expect_eq("directory file count", std::to_string(result.statistics.files), "1");
    test_helpers::expect_eq("directory content", read_file(destination / "report.txt"), "restored");
    test_helpers::expect_true(
        "directory staging removed",
        !fs::exists(root / "output/.btrfs-backup-restore-tx-directory.staging"),
        "staging should not remain"
    );
}

void test_replaces_only_with_explicit_policy() {
    const fs::path root = test_helpers::test_root("restore-engine", "replace");
    test_helpers::write_file(root / "repository/snapshot/report.txt", "new");
    const auto repository = catalog(root / "repository");
    const fs::path destination = root / "output/report.txt";
    test_helpers::write_file(destination, "old");
    const auto result = execute(repository, btrfsbackup::restore::RestoreRequest{
                                                .transaction_id = "tx-replace",
                                                .snapshot_id = "snapshot",
                                                .source_path = btrfsbackup::restore::RelativeRestorePath{"report.txt"},
                                                .destination = destination,
                                                .kind = btrfsbackup::restore::RestoreKind::Files,
                                                .existing_destination = btrfsbackup::restore::ExistingDestinationPolicy::Replace,
                                            });
    test_helpers::expect_true("replacement committed", result.committed, "replacement should commit");
    test_helpers::expect_eq("replacement content", read_file(destination), "new");
    test_helpers::expect_true(
        "previous removed",
        !fs::exists(root / "output/.btrfs-backup-restore-tx-replace.previous"),
        "previous destination should be removed after commit"
    );
}

void test_drill_verifies_and_cleans() {
    const fs::path root = test_helpers::test_root("restore-engine", "drill");
    test_helpers::write_file(root / "repository/snapshot/report.txt", "verified");
    const auto repository = catalog(root / "repository");
    const fs::path destination = root / "drill/result";
    const auto result = execute(repository, btrfsbackup::restore::RestoreRequest{
                                                .transaction_id = "tx-drill",
                                                .snapshot_id = "snapshot",
                                                .source_path = btrfsbackup::restore::RelativeRestorePath{"report.txt"},
                                                .destination = destination,
                                                .kind = btrfsbackup::restore::RestoreKind::Drill,
                                            });
    test_helpers::expect_true("drill result", result.drill && !result.committed, "drill should verify without commit");
    test_helpers::expect_true("drill destination", !fs::exists(destination), "drill must not publish destination");
    test_helpers::expect_true(
        "drill staging removed",
        !fs::exists(root / "drill/.btrfs-backup-restore-tx-drill.staging"),
        "drill staging should be removed"
    );
}

void test_preflight_rejects_source_larger_than_destination_space() {
    const fs::path root = test_helpers::test_root("restore-engine", "insufficient-space");
    const fs::path source = root / "source.bin";
    test_helpers::write_file(source, "x");
    const std::uintmax_t available = fs::space(root).available;
    test_helpers::expect_true(
        "finite destination capacity",
        available < std::numeric_limits<std::uintmax_t>::max(),
        "destination reports an unbounded capacity"
    );
    fs::resize_file(source, available + 1U);

    btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
    btrfsbackup::CancellationToken cancellation;
    try {
        operations.ensure_sufficient_space(source, root / "output/restored.bin", cancellation);
        test_helpers::fail("restore space preflight", "oversized restore was accepted");
    } catch (const btrfsbackup::restore::RestoreError& error) {
        test_helpers::expect_true(
            "restore space preflight code",
            error.code() == btrfsbackup::restore::RestoreErrorCode::InsufficientSpace,
            "oversized restore reported another error"
        );
    }
}

void test_copy_reports_destination_enospc() {
    const fs::path root = test_helpers::test_root("restore-engine", "runtime-enospc");
    const fs::path source = root / "source.bin";
    test_helpers::write_file(source, "content");
    btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
    btrfsbackup::CancellationToken cancellation;
    try {
        (void)operations.copy_and_verify(source, "/dev/full", cancellation);
        test_helpers::fail("restore runtime ENOSPC", "copy to /dev/full was accepted");
    } catch (const btrfsbackup::restore::RestoreError& error) {
        test_helpers::expect_true(
            "restore runtime ENOSPC code",
            error.code() == btrfsbackup::restore::RestoreErrorCode::InsufficientSpace,
            "destination ENOSPC reported another error"
        );
    }
}

} // namespace

int main() {
    test_restores_directory_transactionally();
    test_replaces_only_with_explicit_policy();
    test_drill_verifies_and_cleans();
    test_preflight_rejects_source_larger_than_destination_space();
    test_copy_reports_destination_enospc();
    return test_helpers::finish("transactional restore engine tests passed");
}
