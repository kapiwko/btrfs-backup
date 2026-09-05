// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include <core/Cancellation.hpp>
#include <platform/linux/restore/PosixRestoreOperations.hpp>
#include <restore/RestoreEngine.hpp>
#include <restore/RestoreError.hpp>
#include <restore/RestorePlan.hpp>
#include <support/TestHelpers.hpp>

namespace fs = std::filesystem;

namespace {

void expect_restore_error(
    const std::string& name,
    btrfsbackup::restore::RestoreErrorCode expected,
    const std::function<void()>& operation
) {
    try {
        operation();
        test_helpers::fail(name, "operation unexpectedly succeeded");
    } catch (const btrfsbackup::restore::RestoreError& error) {
        test_helpers::expect_eq(
            name,
            btrfsbackup::restore::restore_error_code_name(error.code()),
            btrfsbackup::restore::restore_error_code_name(expected)
        );
    }
}

btrfsbackup::restore::RepositoryCatalog catalog(const fs::path& root, const std::string& repository_path = "snapshot") {
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
            .repository_path = btrfsbackup::restore::RelativeRestorePath{repository_path},
            .created_at = test_helpers::runtime_time("2026-08-30T120000Z"),
            .uuid = "uuid",
            .received_uuid = "received",
            .parent_uuid = "",
            .verified = true,
        }}
    };
}

class FakeRestoreOperations final : public btrfsbackup::restore::IRestoreOperations {
  public:
    std::set<fs::path> paths;
    bool cancel_during_copy = false;
    bool fail_commit = false;
    bool fail_rollback = false;
    fs::path staging;
    fs::path destination;
    fs::path previous;

    [[nodiscard]] bool exists(const fs::path& path) const override {
        return paths.contains(path);
    }

    void prepare_copy_root(const fs::path&, const fs::path& path) override {
        paths.insert(path);
    }

    void create_subvolume_root(const fs::path& path) override {
        paths.insert(path);
    }

    btrfsbackup::restore::RestoreStatistics copy_and_verify(
        const fs::path&,
        const fs::path&,
        btrfsbackup::CancellationToken& cancellation,
        const btrfsbackup::restore::RestoreProgressSink&
    ) override {
        if (cancel_during_copy) {
            cancellation.request_cancel();
        }
        return btrfsbackup::restore::RestoreStatistics{1, 0, 8};
    }

    void move(const fs::path& source, const fs::path& target) override {
        if (fail_commit && source == staging && target == destination) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::CopyFailed,
                "injected commit failure"
            );
        }
        if (fail_rollback && source == previous && target == destination) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::CopyFailed,
                "injected rollback failure"
            );
        }
        paths.erase(source);
        paths.insert(target);
    }

    void remove_owned_tree(const fs::path& path) override {
        paths.erase(path);
    }
};

btrfsbackup::restore::RestorePlan fake_plan(const fs::path& root) {
    return btrfsbackup::restore::RestorePlan{
        .transaction_id = "tx",
        .snapshot_id = "snapshot",
        .snapshot_uuid = "uuid",
        .source = root / "source",
        .destination = root / "destination",
        .staging = root / ".staging",
        .previous = root / ".previous",
        .kind = btrfsbackup::restore::RestoreKind::Files,
        .existing_destination = btrfsbackup::restore::ExistingDestinationPolicy::Replace,
        .destination_exists = true,
    };
}

void test_rejects_traversal_and_symlink_parents() {
    expect_restore_error("parent traversal", btrfsbackup::restore::RestoreErrorCode::PathTraversal, [] {
        (void)btrfsbackup::restore::RelativeRestorePath{"Documents/../secret"};
    });
    expect_restore_error("absolute traversal", btrfsbackup::restore::RestoreErrorCode::PathTraversal, [] {
        (void)btrfsbackup::restore::RelativeRestorePath{"/etc/passwd"};
    });

    const fs::path root = test_helpers::test_root("restore-safety", "symlink-parent");
    test_helpers::write_file(root / "real/snapshot/Documents/report.txt", "content");
    fs::create_directory_symlink(root / "real/snapshot", root / "repository-link");
    const auto repository = catalog(root, "repository-link");
    btrfsbackup::restore::RestorePlanner planner;
    expect_restore_error("source symlink parent", btrfsbackup::restore::RestoreErrorCode::SymlinkRejected, [&] {
        (void)planner.plan(repository, btrfsbackup::restore::RestoreRequest{
                                           .transaction_id = "tx-source-link",
                                           .snapshot_id = "snapshot",
                                           .source_path = btrfsbackup::restore::RelativeRestorePath{"Documents/report.txt"},
                                           .destination = root / "output/report.txt",
                                       });
    });

    const auto real_repository = catalog(root / "real");
    fs::create_directories(root / "outside");
    fs::create_directory_symlink(root / "outside", root / "destination-link");
    expect_restore_error("destination symlink parent", btrfsbackup::restore::RestoreErrorCode::SymlinkRejected, [&] {
        (void)planner.plan(real_repository, btrfsbackup::restore::RestoreRequest{
                                                .transaction_id = "tx-destination-link",
                                                .snapshot_id = "snapshot",
                                                .source_path = btrfsbackup::restore::RelativeRestorePath{"Documents/report.txt"},
                                                .destination = root / "destination-link/report.txt",
                                            });
    });
}

void test_rejects_symlink_entries_and_cleans_staging() {
    const fs::path root = test_helpers::test_root("restore-safety", "entry-link");
    test_helpers::write_file(root / "repository/snapshot/Documents/report.txt", "content");
    fs::create_symlink("report.txt", root / "repository/snapshot/Documents/report-link");
    const auto repository = catalog(root / "repository");
    btrfsbackup::restore::RestorePlanner planner;
    const auto plan = planner.plan(repository, btrfsbackup::restore::RestoreRequest{
                                                   .transaction_id = "tx-entry-link",
                                                   .snapshot_id = "snapshot",
                                                   .source_path = btrfsbackup::restore::RelativeRestorePath{"Documents"},
                                                   .destination = root / "output/Documents",
                                               });
    btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    btrfsbackup::CancellationToken cancellation;
    expect_restore_error("symlink entry", btrfsbackup::restore::RestoreErrorCode::SymlinkRejected, [&] {
        (void)executor.execute(plan, cancellation);
    });
    test_helpers::expect_true("symlink staging cleanup", !fs::exists(plan.staging), "rejected restore left staging data");
    test_helpers::expect_true("symlink destination absent", !fs::exists(plan.destination), "rejected restore published data");
}

void test_plans_from_a_pinned_source_descriptor() {
    const fs::path root = test_helpers::test_root("restore-safety", "pinned-source");
    const fs::path source = root / "layout/private/snapshot/report.txt";
    test_helpers::write_file(source, "content");
    const int descriptor = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
    test_helpers::expect_true("pinned source open", descriptor >= 0, "could not open pinned source fixture");
    const auto repository = catalog(root);
    const auto plan = btrfsbackup::restore::RestorePlanner{}.plan_from_pinned_source(
        repository,
        {
            .transaction_id = "tx-pinned",
            .snapshot_id = "snapshot",
            .source_path = btrfsbackup::restore::RelativeRestorePath{"report.txt"},
            .destination = root / "output/report.txt",
        },
        fs::path{"/proc/self/fd"} / std::to_string(descriptor)
    );
    test_helpers::expect_true(
        "pinned source plan",
        plan.source == fs::path{"/proc/self/fd"} / std::to_string(descriptor),
        "restore plan did not retain the pinned source"
    );
    btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    btrfsbackup::CancellationToken cancellation;
    (void)executor.execute(plan, cancellation);
    std::ifstream restored(plan.destination);
    std::string content;
    restored >> content;
    test_helpers::expect_eq("pinned source content", content, "content");
    ::close(descriptor);
}

void test_cancellation_cleans_without_commit() {
    const fs::path root = test_helpers::test_root("restore-safety", "cancel");
    const auto plan = fake_plan(root);
    FakeRestoreOperations operations;
    operations.cancel_during_copy = true;
    operations.staging = plan.staging;
    operations.destination = plan.destination;
    operations.previous = plan.previous;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    btrfsbackup::CancellationToken cancellation;
    expect_restore_error("cancel code", btrfsbackup::restore::RestoreErrorCode::Cancelled, [&] {
        (void)executor.execute(plan, cancellation);
    });
    test_helpers::expect_true("cancel staging cleanup", !operations.paths.contains(plan.staging), "cancellation left staging");
    test_helpers::expect_true("cancel no destination", !operations.paths.contains(plan.destination), "cancellation committed destination");
}

void test_commit_failure_restores_previous_destination() {
    const fs::path root = test_helpers::test_root("restore-safety", "rollback");
    const auto plan = fake_plan(root);
    FakeRestoreOperations operations;
    operations.paths.insert(plan.destination);
    operations.fail_commit = true;
    operations.staging = plan.staging;
    operations.destination = plan.destination;
    operations.previous = plan.previous;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    btrfsbackup::CancellationToken cancellation;
    expect_restore_error("commit failure", btrfsbackup::restore::RestoreErrorCode::CopyFailed, [&] {
        (void)executor.execute(plan, cancellation);
    });
    test_helpers::expect_true("rollback destination", operations.paths.contains(plan.destination), "previous destination was not restored");
    test_helpers::expect_true("rollback previous cleanup", !operations.paths.contains(plan.previous), "previous marker remains");
    test_helpers::expect_true("rollback staging cleanup", !operations.paths.contains(plan.staging), "staging remains after rollback");
}

void test_reports_incomplete_rollback() {
    const fs::path root = test_helpers::test_root("restore-safety", "rollback-incomplete");
    const auto plan = fake_plan(root);
    FakeRestoreOperations operations;
    operations.paths.insert(plan.destination);
    operations.fail_commit = true;
    operations.fail_rollback = true;
    operations.staging = plan.staging;
    operations.destination = plan.destination;
    operations.previous = plan.previous;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    btrfsbackup::CancellationToken cancellation;
    expect_restore_error("rollback incomplete", btrfsbackup::restore::RestoreErrorCode::RollbackIncomplete, [&] {
        (void)executor.execute(plan, cancellation);
    });
}

} // namespace

int main() {
    test_rejects_traversal_and_symlink_parents();
    test_rejects_symlink_entries_and_cleans_staging();
    test_plans_from_a_pinned_source_descriptor();
    test_cancellation_cleans_without_commit();
    test_commit_failure_restores_previous_destination();
    test_reports_incomplete_rollback();
    return test_helpers::finish("restore traversal cancellation and rollback tests passed");
}
