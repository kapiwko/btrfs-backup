// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/TransferSizing.hpp>

namespace btrfsbackup::backup::transfer {

TransferPipelinePlan make_stream_sizing_plan(const TransferPipelinePlan& transfer_plan) {
    TransferPipelinePlan sizing_plan = transfer_plan;
    sizing_plan.consumer_argv = {"btrfs", "receive", "--dump"};
    return sizing_plan;
}

} // namespace btrfsbackup::backup::transfer
