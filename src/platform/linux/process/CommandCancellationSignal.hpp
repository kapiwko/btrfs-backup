// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <stop_token>

#include <core/Cancellation.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux::process {

class CommandCancellationSignal final {
  public:
    explicit CommandCancellationSignal(const CancellationToken& cancellation);
    CommandCancellationSignal(const CommandCancellationSignal&) = delete;
    CommandCancellationSignal& operator=(const CommandCancellationSignal&) = delete;
    CommandCancellationSignal(CommandCancellationSignal&&) = delete;
    CommandCancellationSignal& operator=(CommandCancellationSignal&&) = delete;
    ~CommandCancellationSignal() noexcept;

    [[nodiscard]] int fd() const noexcept;

  private:
    struct WriteSignal {
        int fd = -1;
        void operator()() const noexcept;
    };

    OwnedFileDescriptor read_fd_;
    OwnedFileDescriptor write_fd_;
    std::optional<std::stop_callback<WriteSignal>> callback_;
};

} // namespace btrfsbackup::platform::linux::process
