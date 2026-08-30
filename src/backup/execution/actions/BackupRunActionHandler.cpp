// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/execution/actions/BackupRunActionHandler.hpp>

#include <stdexcept>

#include <backup/execution/actions/HookActionHandler.hpp>
#include <backup/execution/actions/RecoveryActionHandler.hpp>
#include <backup/execution/actions/RepositoryActionHandler.hpp>
#include <backup/execution/actions/RetentionActionHandler.hpp>
#include <backup/execution/actions/SnapshotActionHandler.hpp>

namespace btrfsbackup::backup::execution {

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
    RepositoryActionHandler& repository
)
    : snapshots_(snapshots),
      recovery_(recovery),
      retention_(retention),
      hooks_(hooks),
      repository_(repository) {
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
                   [](const SendReceiveAction&) {
                       throw std::logic_error("send-receive actions are handled by TransferCoordinator");
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

} // namespace btrfsbackup::backup::execution
