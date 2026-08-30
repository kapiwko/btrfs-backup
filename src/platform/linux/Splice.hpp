// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <sys/types.h>

namespace btrfsbackup::platform::linux {

ssize_t splice_pipe(int source_fd, int target_fd, std::size_t max_bytes);

} // namespace btrfsbackup::platform::linux
