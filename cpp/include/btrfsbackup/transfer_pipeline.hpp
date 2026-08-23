#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace btrfsbackup {

struct TransferPipelinePlan {
    std::vector<std::string> producer_argv;
    std::vector<std::string> consumer_argv;
};

enum class TransferEventKind {
    Started,
    Progress,
    ProducerFinished,
    ConsumerFinished,
    Cancelled,
    Completed,
};

struct TransferEvent {
    TransferEventKind kind = TransferEventKind::Started;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t pending_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::string message;
};

class ITransferEventSink {
public:
    virtual ~ITransferEventSink() = default;
    virtual void on_transfer_event(const TransferEvent& event) = 0;
};

class NullTransferEventSink final : public ITransferEventSink {
public:
    void on_transfer_event(const TransferEvent& event) override;
};

class CancellationToken {
public:
    CancellationToken();
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;
    ~CancellationToken();

    void request_cancel();
    bool cancellation_requested() const;
    int cancellation_fd() const;
    void drain_cancellation_signal() const;

private:
    int cancellation_pipe_[2] = {-1, -1};
    std::atomic_bool cancellation_requested_ = false;
};

struct TransferSideResult {
    bool started = false;
    int exit_code = -1;
    std::string diagnostics;
};

struct TransferResult {
    TransferSideResult producer;
    TransferSideResult consumer;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t duration_ms = 0;
    std::uint64_t average_speed_bps = 0;
    bool cancelled = false;
};

class ITransferPipeline {
public:
    virtual ~ITransferPipeline() = default;
    virtual TransferResult run(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events,
        CancellationToken& cancellation
    ) = 0;
};

class PosixTransferPipeline final : public ITransferPipeline {
public:
    TransferResult run(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events,
        CancellationToken& cancellation
    ) override;
};

class IAsyncTransferHandle {
public:
    virtual ~IAsyncTransferHandle() = default;
    virtual bool finished() const = 0;
    virtual int completion_fd() const = 0;
    virtual void request_cancel() = 0;
    virtual TransferResult wait() = 0;
};

class IAsyncTransferPipeline {
public:
    virtual ~IAsyncTransferPipeline() = default;
    virtual std::unique_ptr<IAsyncTransferHandle> start(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events
    ) = 0;
};

class ThreadedAsyncTransferPipeline final : public IAsyncTransferPipeline {
public:
    explicit ThreadedAsyncTransferPipeline(ITransferPipeline& pipeline);

    std::unique_ptr<IAsyncTransferHandle> start(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events
    ) override;

private:
    ITransferPipeline& pipeline_;
};

bool transfer_succeeded(const TransferResult& result);
std::string transfer_failure_error_code(const TransferResult& result);
void require_transfer_success(const TransferResult& result);

} // namespace btrfsbackup
