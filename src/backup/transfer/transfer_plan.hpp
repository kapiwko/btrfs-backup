// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace btrfsbackup {

class ITransferResource {
  public:
    virtual ~ITransferResource() = default;
};

struct TransferPipelinePlan {
    std::vector<std::string> producer_argv;
    std::vector<std::string> consumer_argv;
    std::uint64_t bytes_total_estimated = 0;
    std::vector<std::shared_ptr<ITransferResource>> retained_resources;
};

} // namespace btrfsbackup
