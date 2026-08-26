// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run_action_handler.hpp>

#include <backup/hook_action_handler.hpp>
#include <backup/recovery_action_handler.hpp>
#include <backup/repository_action_handler.hpp>
#include <backup/retention_action_handler.hpp>
#include <backup/snapshot_action_handler.hpp>
#include <backup/transfer_action_handler.hpp>

namespace btrfsbackup {

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
                   [](const SelectParentAction&) {},
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

} // namespace btrfsbackup
