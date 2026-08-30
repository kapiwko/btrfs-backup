// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/TransferChildTermination.hpp>

#include <signal.h>

#include <string>

#include <platform/linux/PosixTransferProcess.hpp>

namespace btrfsbackup::platform::linux {

namespace {

void append_diagnostic(std::string& diagnostics, const std::string& message) {
    if (!diagnostics.empty())
        diagnostics += '\n';
    diagnostics += message;
}

} // namespace

TransferChildTermination::TransferChildTermination(
    ChildProcess& process,
    std::chrono::milliseconds terminate_grace_period,
    std::chrono::milliseconds kill_reap_period
) noexcept
    : process_(process),
      terminate_grace_period_(terminate_grace_period),
      kill_reap_period_(kill_reap_period) {
}

void TransferChildTermination::request(bool child_done) {
    if (terminate_sent_ || process_.pid() <= 0 || (child_done && !process_.process_group_exists()))
        return;
    process_.send_signal(SIGTERM);
    terminate_sent_ = true;
    deadline_ = std::chrono::steady_clock::now() + terminate_grace_period_;
}

ChildTerminationProgress TransferChildTermination::advance(
    bool& child_done,
    btrfsbackup::backup::transfer::TransferSideResult& side
) {
    if (!terminate_sent_ || !deadline_.has_value())
        return ChildTerminationProgress::None;
    if (child_done && !process_.process_group_exists()) {
        deadline_.reset();
        return ChildTerminationProgress::None;
    }
    if (std::chrono::steady_clock::now() < *deadline_)
        return ChildTerminationProgress::None;
    if (!kill_sent_) {
        process_.send_signal(SIGKILL);
        kill_sent_ = true;
        deadline_ = std::chrono::steady_clock::now() + kill_reap_period_;
        append_diagnostic(side.diagnostics(), "did not exit after SIGTERM; sent SIGKILL");
        return ChildTerminationProgress::None;
    }

    deadline_.reset();
    if (child_done)
        return ChildTerminationProgress::Abandoned;
    if (reap_posix_transfer_process(process_.pid(), side)) {
        child_done = true;
        process_.mark_reaped();
        return ChildTerminationProgress::Reaped;
    }
    side.mark_exited(128 + SIGKILL);
    append_diagnostic(side.diagnostics(), "did not become waitable after SIGKILL");
    child_done = true;
    process_.release();
    return ChildTerminationProgress::Abandoned;
}

bool TransferChildTermination::pending() const noexcept {
    return terminate_sent_ && deadline_.has_value();
}

} // namespace btrfsbackup::platform::linux
