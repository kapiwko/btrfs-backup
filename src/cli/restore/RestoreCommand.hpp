// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ostream>
#include <string>
#include <vector>

#include <core/Cancellation.hpp>

namespace btrfsbackup::cli::restore {

int restore(const std::vector<std::string>& args, std::ostream& output, CancellationToken& cancellation);

} // namespace btrfsbackup::cli::restore
