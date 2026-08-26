// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/transfer_result.hpp>

#include <config/errors.hpp>

namespace btrfsbackup {

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

bool transfer_succeeded(const TransferResult& result) {
    return !result.cancelled
        && result.producer.started
        && result.consumer.started
        && result.producer.exit_code == 0
        && result.consumer.exit_code == 0;
}

std::optional<ErrorCode> transfer_failure_error_code(const TransferResult& result) {
    if (transfer_succeeded(result)) {
        return std::nullopt;
    }
    if (result.cancelled) {
        return ErrorCode::RunnerCancelled;
    }

    const bool producer_failed = !result.producer.started || result.producer.exit_code != 0;
    const bool consumer_failed = !result.consumer.started || result.consumer.exit_code != 0;
    if (producer_failed && consumer_failed) {
        return ErrorCode::TransferProducerConsumerFailed;
    }
    if (producer_failed) {
        return ErrorCode::TransferProducerFailed;
    }
    if (consumer_failed) {
        return ErrorCode::TransferConsumerFailed;
    }
    return ErrorCode::TransferFailed;
}

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
