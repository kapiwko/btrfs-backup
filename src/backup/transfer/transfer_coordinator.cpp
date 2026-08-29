// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/transfer_coordinator.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <utility>

#include <backup/ports/safe_directory.hpp>
#include <backup/snapshot_transfer.hpp>
#include <core/cancellation.hpp>
#include <core/error_code.hpp>
#include <core/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::backup::transfer {

namespace {

TransferPipelinePlan transfer_plan_for_action(
    const SendReceiveAction& action,
    const fs::path& target_mount_point,
    const ISafeDirectoryRootFactory& safe_directories
) {
    std::unique_ptr<ISafeDirectoryRoot> local_root = safe_directories.open("/");
    std::unique_ptr<ISafeDirectoryRoot> target_root = safe_directories.open(target_mount_point);
    target_root->ensure_directory(action.remote_snapshot_directory);
    target_root->ensure_directory(action.incoming_run_directory);

    std::shared_ptr<ISafeDirectoryHandle> snapshot = local_root->pin_directory(action.snapshot);
    std::shared_ptr<ISafeDirectoryHandle> receive = target_root->pin_directory(action.incoming_run_directory);
    std::shared_ptr<ISafeDirectoryHandle> parent;
    fs::path parent_path;
    if (action.parent.has_value()) {
        parent = local_root->pin_directory(*action.parent);
        parent_path = parent->stable_path();
    }

    SendReceiveCommandPlan command_plan = build_send_receive_command_plan(
        snapshot->stable_path(),
        parent_path,
        receive->stable_path()
    );
    TransferPipelinePlan plan{
        .producer_argv = std::move(command_plan.send_argv),
        .consumer_argv = std::move(command_plan.receive_argv),
        .retained_resources = {snapshot, receive},
    };
    if (parent) {
        plan.retained_resources.push_back(parent);
    }
    return plan;
}

void wait_for_transfer_or_cancellation(IAsyncTransferHandle& transfer, CancellationToken& cancellation) {
    while (!transfer.wait_for(std::chrono::milliseconds(100))) {
        if (cancellation.cancellation_requested()) {
            transfer.request_cancel();
        }
    }
}

void require_success_with_error_code(const TransferResult& result) {
    try {
        require_transfer_success(result);
    } catch (const ValidationError& error) {
        const ErrorCode code = transfer_failure_error_code(result).value_or(ErrorCode::TransferFailed);
        throw CodedValidationError(code, error.what());
    }
}

} // namespace

TransferCoordinator::TransferCoordinator(
    IAsyncTransferPipeline& pipeline,
    const ISafeDirectoryRootFactory& safe_directories
)
    : pipeline_(pipeline), safe_directories_(safe_directories) {
}

TransferResult TransferCoordinator::execute(
    const SendReceiveAction& action,
    const fs::path& target_mount_point,
    ITransferEventSink& events,
    CancellationToken& cancellation
) {
    TransferPipelinePlan plan = transfer_plan_for_action(action, target_mount_point, safe_directories_);

    std::unique_ptr<IAsyncTransferHandle> transfer = pipeline_.start(plan, events);
    wait_for_transfer_or_cancellation(*transfer, cancellation);
    TransferResult result = transfer->wait();
    if (!result.cancelled) {
        require_success_with_error_code(result);
    }
    return result;
}

} // namespace btrfsbackup::backup::transfer
