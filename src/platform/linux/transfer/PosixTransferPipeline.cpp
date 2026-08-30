// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/transfer/PosixTransferPipeline.hpp>

#include <core/Errors.hpp>
#include <platform/linux/transfer/PosixTransferSession.hpp>

namespace btrfsbackup::platform::linux::transfer {

PosixTransferPipeline::PosixTransferPipeline(TransferTerminationPolicy termination_policy)
    : termination_policy_(termination_policy) {
    if (termination_policy_.terminate_grace_period.count() <= 0 || termination_policy_.kill_reap_period.count() <= 0) {
        throw ValidationError("transfer termination periods must be positive");
    }
}

btrfsbackup::backup::transfer::TransferResult PosixTransferPipeline::run(
    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    CancellationToken& cancellation
) {
    PosixTransferSession session(plan, events, cancellation, termination_policy_);
    return session.run();
}

} // namespace btrfsbackup::platform::linux::transfer
