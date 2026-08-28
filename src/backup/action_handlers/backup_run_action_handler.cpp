// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/backup_run_action_handler.hpp>

#include <backup/action_handlers/hook_action_handler.hpp>
#include <backup/action_handlers/recovery_action_handler.hpp>
#include <backup/action_handlers/repository_action_handler.hpp>
#include <backup/action_handlers/retention_action_handler.hpp>
#include <backup/action_handlers/snapshot_action_handler.hpp>
#include <backup/action_handlers/transfer_action_handler.hpp>

namespace btrfsbackup::backup {

namespace {

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

} // namespace

BackupRunActionHandler::BackupRunActionHandler(
    SnapshotActionHandler& snapshots,
    RecoveryActionHandler& recovery,
    RetentionActionHandler& retention,
    HookActionHandler& hooks,
    RepositoryActionHandler& repository,
    TransferActionHandler& transfers
)
    : snapshots_(snapshots),
      recovery_(recovery),
      retention_(retention),
      hooks_(hooks),
      repository_(repository),
      transfers_(transfers) {
}

void BackupRunActionHandler::handle(
    const BackupRunAction& action,
    const BackupRunPlan& run_plan,
    CancellationToken& cancellation
) {
    std::visit(Overloaded{
                   [&](const RecoverPendingAction& typed_action) {
                       recovery_.handle(typed_action);
                   },
                   [&](const CleanupIncomingAction& typed_action) {
                       repository_.handle(typed_action);
                   },
                   [&](const RunHookAction& typed_action) {
                       hooks_.handle(typed_action, run_plan.profile_id, cancellation);
                   },
                   [&](const CreateSnapshotAction& typed_action) {
                       snapshots_.handle(typed_action);
                   },
                   [&](const SendReceiveAction& typed_action) {
                       transfers_.handle(typed_action);
                   },
                   [&](const VerifyReceivedAction& typed_action) {
                       repository_.handle(typed_action);
                   },
                   [&](const CommitReceivedAction& typed_action) {
                       repository_.handle(typed_action);
                   },
                   [&](const ApplyRemoteRetentionAction& typed_action) {
                       retention_.handle(typed_action);
                   },
                   [&](const ApplyLocalRetentionAction& typed_action) {
                       retention_.handle(typed_action);
                   },
                   [&](const CleanupSourceAction& typed_action) {
                       repository_.handle(typed_action);
                   },
               },
               action);
}

} // namespace btrfsbackup::backup
