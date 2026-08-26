// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace btrfsbackup {

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
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t delta_bytes = 0;
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

} // namespace btrfsbackup
