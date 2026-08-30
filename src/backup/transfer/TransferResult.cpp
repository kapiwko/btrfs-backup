// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/TransferResult.hpp>

#include <utility>

#include <core/Errors.hpp>

namespace btrfsbackup::backup::transfer {

TransferSideResult::TransferSideResult(std::variant<TransferNotStarted, TransferRunning, TransferExited> state)
    : state_(std::move(state)) {
}

TransferSideResult TransferSideResult::not_started(std::string diagnostics) {
    return TransferSideResult{TransferNotStarted{std::move(diagnostics)}};
}

TransferSideResult TransferSideResult::running() {
    return TransferSideResult{TransferRunning{}};
}

TransferSideResult TransferSideResult::exited(int exit_code, std::string diagnostics) {
    return TransferSideResult{TransferExited{exit_code, std::move(diagnostics)}};
}

bool TransferSideResult::started() const noexcept {
    return !std::holds_alternative<TransferNotStarted>(state_);
}

std::optional<int> TransferSideResult::exit_code() const noexcept {
    if (const auto* exited = std::get_if<TransferExited>(&state_)) {
        return exited->exit_code;
    }
    return std::nullopt;
}

const std::string& TransferSideResult::diagnostics() const noexcept {
    return std::visit([](const auto& state) -> const std::string& { return state.diagnostics; }, state_);
}

std::string& TransferSideResult::diagnostics() noexcept {
    return std::visit([](auto& state) -> std::string& { return state.diagnostics; }, state_);
}

void TransferSideResult::mark_exited(int exit_code) {
    state_ = TransferExited{exit_code, std::move(diagnostics())};
}

namespace {

std::string side_failure(const char* side, const TransferSideResult& result) {
    std::string message = std::string(side) + " failed";
    if (!result.started()) {
        message += " before start";
    } else {
        message += " with exit code " + std::to_string(result.exit_code().value_or(-1));
    }
    if (!result.diagnostics().empty()) {
        message += ": " + result.diagnostics();
    }
    return message;
}

} // namespace

bool transfer_succeeded(const TransferResult& result) {
    return !result.cancelled && result.producer.exit_code() == 0 && result.consumer.exit_code() == 0;
}

std::optional<ErrorCode> transfer_failure_error_code(const TransferResult& result) {
    if (transfer_succeeded(result)) {
        return std::nullopt;
    }
    if (result.cancelled) {
        return ErrorCode::RunnerCancelled;
    }

    const bool producer_failed = result.producer.exit_code() != 0;
    const bool consumer_failed = result.consumer.exit_code() != 0;
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

    const bool producer_failed = result.producer.exit_code() != 0;
    const bool consumer_failed = result.consumer.exit_code() != 0;
    if (producer_failed && consumer_failed) {
        throw ValidationError(
            "Transfer failed: " + side_failure("producer", result.producer) + "; " + side_failure("consumer", result.consumer)
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

} // namespace btrfsbackup::backup::transfer
