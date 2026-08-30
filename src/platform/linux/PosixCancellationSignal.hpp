// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <stop_token>

#include <core/Cancellation.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux {

class PosixCancellationSignal {
  public:
    explicit PosixCancellationSignal(const CancellationToken& cancellation);
    PosixCancellationSignal(const PosixCancellationSignal&) = delete;
    PosixCancellationSignal& operator=(const PosixCancellationSignal&) = delete;
    ~PosixCancellationSignal() noexcept;

    [[nodiscard]] int fd() const noexcept;
    void drain() const noexcept;

  private:
    struct WriteSignal {
        int fd = -1;
        void operator()() const noexcept;
    };

    OwnedFileDescriptor read_fd_;
    OwnedFileDescriptor write_fd_;
    std::optional<std::stop_callback<WriteSignal>> callback_;
};

} // namespace btrfsbackup::platform::linux
