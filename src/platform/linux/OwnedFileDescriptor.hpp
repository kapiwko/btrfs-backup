// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace btrfsbackup::platform::linux {

class OwnedFileDescriptor final {
public:
    OwnedFileDescriptor() noexcept = default;
    explicit OwnedFileDescriptor(int descriptor) noexcept;
    ~OwnedFileDescriptor() noexcept;

    OwnedFileDescriptor(const OwnedFileDescriptor&) = delete;
    OwnedFileDescriptor& operator=(const OwnedFileDescriptor&) = delete;

    OwnedFileDescriptor(OwnedFileDescriptor&& other) noexcept;
    OwnedFileDescriptor& operator=(OwnedFileDescriptor&& other) noexcept;

    [[nodiscard]] int get() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;
    void reset(int descriptor = -1) noexcept;

private:
    int descriptor_{-1};
};

} // namespace btrfsbackup::platform::linux
