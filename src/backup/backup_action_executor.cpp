// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_action_executor.hpp>

#include <type_traits>

namespace btrfsbackup::backup {

namespace {

class BackupTransferEventAdapter final : public btrfsbackup::backup::transfer::ITransferEventSink {
  public:
    explicit BackupTransferEventAdapter(BackupActionExecutionContext& context)
        : context_(context) {
    }

    void on_transfer_event(const btrfsbackup::backup::transfer::TransferEvent& event) override {
        if (event.kind != btrfsbackup::backup::transfer::TransferEventKind::Progress) {
            return;
        }
        context_.events.on_backup_run_event(TransferProgress{
            .profile_id = context_.plan.profile_id,
            .run_id = context_.plan.run_id,
            .source_id = context_.source.source_id,
            .source_index = context_.source_index,
            .bytes_transferred = event.bytes_transferred,
            .bytes_produced = event.bytes_produced,
            .bytes_total_estimated = event.bytes_total_estimated,
            .run_bytes_transferred = context_.completed_run_bytes + event.bytes_transferred,
            .delta_bytes = event.delta_bytes,
            .elapsed_ms = event.elapsed_ms,
            .speed_bps = event.speed_bps,
            .message = event.message,
        });
    }

  private:
    BackupActionExecutionContext& context_;
};

} // namespace

BackupActionExecutor::BackupActionExecutor(
    IBackupRunActionHandler& action_handler,
    btrfsbackup::backup::transfer::IAsyncTransferPipeline& transfer_pipeline,
    const ISafeDirectoryRootFactory& safe_directories
)
    : action_handler_(action_handler), transfer_coordinator_(transfer_pipeline, safe_directories) {
}

BackupActionExecutionResult BackupActionExecutor::execute(
    const BackupRunAction& action,
    BackupActionExecutionContext& context
) {
    return std::visit([&](const auto& typed_action) -> BackupActionExecutionResult {
        using Action = std::decay_t<decltype(typed_action)>;
        if constexpr (std::is_same_v<Action, SendReceiveAction>) {
            BackupTransferEventAdapter transfer_events(context);
            const btrfsbackup::backup::transfer::TransferResult result = transfer_coordinator_.execute(
                typed_action,
                context.plan.target_mount_point,
                transfer_events,
                context.cancellation
            );
            return {
                .cancelled = result.cancelled,
                .bytes_transferred = result.bytes_transferred,
            };
        } else {
            action_handler_.handle(action, context.plan, context.cancellation);
            return {};
        }
    },
                      action);
}

} // namespace btrfsbackup::backup
