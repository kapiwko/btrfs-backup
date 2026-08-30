// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <backup/transfer/ITransferPipeline.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <platform/linux/process/ChildProcess.hpp>
#include <platform/linux/process/ProcessSpawn.hpp>
#include <platform/linux/transfer/BoundedDiagnosticBuffer.hpp>
#include <platform/linux/transfer/PosixCancellationSignal.hpp>
#include <platform/linux/transfer/PosixTransferPipeline.hpp>
#include <platform/linux/transfer/ThreadSigpipeBlock.hpp>
#include <platform/linux/transfer/TransferChildTermination.hpp>
#include <platform/linux/transfer/TransferProgressReporter.hpp>

namespace btrfsbackup::platform::linux::transfer {

class PosixTransferRuntime final {
  public:
    PosixTransferRuntime(
        const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        CancellationToken& cancellation,
        TransferTerminationPolicy termination_policy
    );

    PosixTransferRuntime(const PosixTransferRuntime&) = delete;
    PosixTransferRuntime& operator=(const PosixTransferRuntime&) = delete;
    PosixTransferRuntime(PosixTransferRuntime&&) = delete;
    PosixTransferRuntime& operator=(PosixTransferRuntime&&) = delete;

    void initialize_transfer();
    void spawn_sides();
    void emit_started();
    void close_child_endpoints();
    void run_event_loop();
    void finalize_diagnostics();
    void emit_completed();
    [[nodiscard]] btrfsbackup::backup::transfer::TransferResult take_result();

  private:
    [[nodiscard]] bool loop_pending() const;
    void cancel_transfer();
    void advance_terminations();
    void poll_once();
    void pump_ready_data();
    void reap_children();

    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan_;
    btrfsbackup::backup::transfer::ITransferEventSink& events_;
    CancellationToken& cancellation_;
    TransferTerminationPolicy termination_policy_;
    TransferSteadyClock::time_point started_at_;
    TransferProgressReporter progress_reporter_;
    ThreadSigpipeBlock sigpipe_block_;
    PosixCancellationSignal cancellation_signal_;
    OwnedFileDescriptor data_read_end_;
    OwnedFileDescriptor data_write_end_;
    OwnedFileDescriptor consumer_input_read_end_;
    OwnedFileDescriptor consumer_input_write_end_;
    OwnedFileDescriptor producer_error_read_end_;
    OwnedFileDescriptor producer_error_write_end_;
    OwnedFileDescriptor consumer_error_read_end_;
    OwnedFileDescriptor consumer_error_write_end_;
    OwnedFileDescriptor dev_null_;
    process::ProcessSpawnResult producer_spawn_;
    process::ProcessSpawnResult consumer_spawn_;
    process::ChildProcess producer_process_;
    process::ChildProcess consumer_process_;
    std::optional<TransferChildTermination> producer_termination_;
    std::optional<TransferChildTermination> consumer_termination_;
    btrfsbackup::backup::transfer::TransferResult result_;
    BoundedDiagnosticBuffer producer_stderr_;
    BoundedDiagnosticBuffer consumer_stderr_;
    bool producer_stdout_open_ = false;
    bool consumer_stdin_open_ = false;
    bool producer_stdout_ready_ = false;
    bool consumer_stdin_ready_ = false;
    bool producer_stderr_open_ = false;
    bool consumer_stderr_open_ = false;
    bool producer_done_ = true;
    bool consumer_done_ = true;
    bool cancellation_sent_ = false;
};

} // namespace btrfsbackup::platform::linux::transfer
