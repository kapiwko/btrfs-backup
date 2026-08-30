// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/transfer/PosixTransferSession.hpp>

#include <platform/linux/transfer/PosixTransferRuntime.hpp>

namespace btrfsbackup::platform::linux::transfer {

PosixTransferSession::PosixTransferSession(
    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    CancellationToken& cancellation,
    TransferTerminationPolicy termination_policy
)
    : plan_(plan), events_(events), cancellation_(cancellation), termination_policy_(termination_policy) {
}

btrfsbackup::backup::transfer::TransferResult PosixTransferSession::run() {
    PosixTransferRuntime runtime(plan_, events_, cancellation_, termination_policy_);
    runtime.initialize_transfer();
    runtime.spawn_sides();
    runtime.emit_started();
    runtime.close_child_endpoints();
    runtime.run_event_loop();
    runtime.finalize_diagnostics();
    runtime.emit_completed();
    return runtime.take_result();
}

} // namespace btrfsbackup::platform::linux::transfer
