// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/transfer/TransferPlan.hpp>

namespace btrfsbackup::backup::transfer {

[[nodiscard]] TransferPipelinePlan make_stream_sizing_plan(const TransferPipelinePlan& transfer_plan);

} // namespace btrfsbackup::backup::transfer
