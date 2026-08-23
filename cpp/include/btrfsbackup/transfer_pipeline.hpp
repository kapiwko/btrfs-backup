#pragma once

#include <atomic>
#include <cstdint>
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
    void request_cancel();
    bool cancellation_requested() const;

private:
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

bool transfer_succeeded(const TransferResult& result);
void require_transfer_success(const TransferResult& result);

} // namespace btrfsbackup
