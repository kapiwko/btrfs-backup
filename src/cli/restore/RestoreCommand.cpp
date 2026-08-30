// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/restore/RestoreCommand.hpp>

#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <vector>

#include <platform/linux/restore/PosixRestoreOperations.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <restore/RepositoryDiscoveryService.hpp>
#include <restore/RestoreEngine.hpp>
#include <restore/RestoreError.hpp>
#include <restore/RestorePlan.hpp>
#include <restore/SnapshotBrowser.hpp>

namespace btrfsbackup::cli::restore {

namespace {

struct Options {
    std::string command;
    std::filesystem::path repository;
    std::string snapshot;
    std::string source = ".";
    std::filesystem::path destination;
    std::string transaction;
    std::string host;
    std::string profile;
    std::string source_id;
    bool replace = false;
    bool subvolume = false;
};

std::string option_value(const std::vector<std::string>& args, std::size_t& index) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            args.at(index) + " requires a value"
        );
    }
    return args.at(++index);
}

Options parse_options(const std::vector<std::string>& args) {
    if (args.empty()) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            "restore command is required"
        );
    }
    Options options;
    options.command = args.at(0);
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& argument = args.at(index);
        if (argument == "--repository") options.repository = option_value(args, index);
        else if (argument == "--snapshot") options.snapshot = option_value(args, index);
        else if (argument == "--source") options.source = option_value(args, index);
        else if (argument == "--destination") options.destination = option_value(args, index);
        else if (argument == "--transaction") options.transaction = option_value(args, index);
        else if (argument == "--host") options.host = option_value(args, index);
        else if (argument == "--profile") options.profile = option_value(args, index);
        else if (argument == "--source-id") options.source_id = option_value(args, index);
        else if (argument == "--replace") options.replace = true;
        else if (argument == "--subvolume") options.subvolume = true;
        else {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::PathInvalid,
                "unknown restore option: " + argument
            );
        }
    }
    if (options.repository.empty()) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::RepositoryNotFound,
            "--repository is required"
        );
    }
    return options;
}

btrfsbackup::restore::RepositoryCatalog discover(const std::filesystem::path& root) {
    btrfsbackup::restore::RepositoryDiscoveryService discovery([](const std::filesystem::path& path) {
        const auto metadata = btrfsbackup::platform::linux::storage::read_btrfs_snapshot_metadata(path);
        if (!metadata.has_value()) {
            return std::optional<btrfsbackup::restore::DiscoveredSnapshotMetadata>{};
        }
        return std::optional<btrfsbackup::restore::DiscoveredSnapshotMetadata>{
            btrfsbackup::restore::DiscoveredSnapshotMetadata{
                .is_subvolume = metadata->is_subvolume,
                .readonly = metadata->readonly,
                .uuid = metadata->uuid.value(),
                .received_uuid = metadata->received_uuid.value(),
            }
        };
    });
    return discovery.discover(root);
}

btrfsbackup::restore::RestorePlan make_plan(
    const btrfsbackup::restore::RepositoryCatalog& catalog,
    const Options& options,
    bool drill
) {
    if (options.snapshot.empty() || options.destination.empty() || options.transaction.empty()) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            "--snapshot, --source, --destination and --transaction are required"
        );
    }
    btrfsbackup::restore::RestorePlanner planner;
    return planner.plan(catalog, btrfsbackup::restore::RestoreRequest{
        .transaction_id = options.transaction,
        .snapshot_id = options.snapshot,
        .source_path = btrfsbackup::restore::RelativeRestorePath{options.source},
        .destination = options.destination,
        .kind = drill ? btrfsbackup::restore::RestoreKind::Drill
                      : (options.subvolume ? btrfsbackup::restore::RestoreKind::Subvolume
                                           : btrfsbackup::restore::RestoreKind::Files),
        .existing_destination = options.replace ? btrfsbackup::restore::ExistingDestinationPolicy::Replace
                                                : btrfsbackup::restore::ExistingDestinationPolicy::Fail,
    });
}

void print_plan(std::ostream& output, const btrfsbackup::restore::RestorePlan& plan) {
    std::println(output, "transaction: {}", plan.transaction_id);
    std::println(output, "snapshot: {} ({})", plan.snapshot_id, plan.snapshot_uuid);
    std::println(output, "source: {}", plan.source.string());
    std::println(output, "destination: {}", plan.destination.string());
    std::println(output, "existing destination: {}", plan.destination_exists ? "yes" : "no");
}

} // namespace

int restore(const std::vector<std::string>& args, std::ostream& output, CancellationToken& cancellation) {
    const Options options = parse_options(args);
    const btrfsbackup::restore::RepositoryCatalog catalog = discover(options.repository);
    if (options.command == "catalog") {
        std::println(output, "repository: {}", catalog.identity().repository_id);
        for (const btrfsbackup::restore::CatalogSnapshot& snapshot : catalog.snapshots()) {
            std::println(output, "{} {} {} {}", snapshot.snapshot_id, snapshot.host_id, snapshot.profile_id, snapshot.source_id);
        }
        return 0;
    }
    if (options.command == "list") {
        btrfsbackup::restore::SnapshotBrowser browser;
        for (const btrfsbackup::restore::SnapshotEntry& entry : browser.list(
                 catalog,
                 options.snapshot,
                 btrfsbackup::restore::RelativeRestorePath{options.source}
             )) {
            std::println(output, "{}", entry.name);
        }
        return 0;
    }
    if (options.command == "versions") {
        for (const btrfsbackup::restore::CatalogSnapshot* snapshot : btrfsbackup::restore::find_versions(
                 catalog,
                 options.host,
                 options.profile,
                 options.source_id,
                 btrfsbackup::restore::RelativeRestorePath{options.source}
             )) {
            std::println(output, "{}", snapshot->snapshot_id);
        }
        return 0;
    }
    if (options.command != "plan" && options.command != "execute" && options.command != "drill") {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            "unknown restore command: " + options.command
        );
    }
    const btrfsbackup::restore::RestorePlan plan = make_plan(catalog, options, options.command == "drill");
    print_plan(output, plan);
    if (options.command == "plan") {
        return 0;
    }
    btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
    btrfsbackup::restore::RestoreExecutor executor(operations);
    const btrfsbackup::restore::RestoreResult result = executor.execute(plan, cancellation);
    std::println(
        output,
        "{}: {} files, {} directories, {} bytes",
        result.drill ? "drill verified" : "restore committed",
        result.statistics.files,
        result.statistics.directories,
        result.statistics.bytes
    );
    return 0;
}

} // namespace btrfsbackup::cli::restore
