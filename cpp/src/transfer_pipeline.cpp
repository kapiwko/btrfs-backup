#include <btrfsbackup/transfer_pipeline.hpp>

#include <string>

#include <btrfsbackup/errors.hpp>

namespace btrfsbackup {

void NullTransferEventSink::on_transfer_event(const TransferEvent&) {
}

void CancellationToken::request_cancel() {
    cancellation_requested_.store(true);
}

bool CancellationToken::cancellation_requested() const {
    return cancellation_requested_.load();
}

bool transfer_succeeded(const TransferResult& result) {
    return !result.cancelled
        && result.producer.started
        && result.consumer.started
        && result.producer.exit_code == 0
        && result.consumer.exit_code == 0;
}

namespace {

std::string side_failure(const char* side, const TransferSideResult& result) {
    std::string message = std::string(side) + " failed";
    if (!result.started) {
        message += " before start";
    } else {
        message += " with exit code " + std::to_string(result.exit_code);
    }
    if (!result.diagnostics.empty()) {
        message += ": " + result.diagnostics;
    }
    return message;
}

} // namespace

void require_transfer_success(const TransferResult& result) {
    if (transfer_succeeded(result)) {
        return;
    }
    if (result.cancelled) {
        throw ValidationError("Transfer was cancelled");
    }

    const bool producer_failed = !result.producer.started || result.producer.exit_code != 0;
    const bool consumer_failed = !result.consumer.started || result.consumer.exit_code != 0;
    if (producer_failed && consumer_failed) {
        throw ValidationError(
            "Transfer failed: "
            + side_failure("producer", result.producer)
            + "; "
            + side_failure("consumer", result.consumer)
        );
    }
    if (producer_failed) {
        throw ValidationError(side_failure("producer", result.producer));
    }
    if (consumer_failed) {
        throw ValidationError(side_failure("consumer", result.consumer));
    }
    throw ValidationError("Transfer failed");
}

} // namespace btrfsbackup
