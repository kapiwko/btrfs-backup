// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/TransferEvent.hpp>

namespace btrfsbackup::backup::transfer {

void NullTransferEventSink::on_transfer_event(const TransferEvent&) {
}

} // namespace btrfsbackup::backup::transfer
