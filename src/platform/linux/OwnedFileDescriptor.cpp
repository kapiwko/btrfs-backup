// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/OwnedFileDescriptor.hpp>

#include <utility>

#include <unistd.h>

namespace btrfsbackup::platform::linux {

OwnedFileDescriptor::OwnedFileDescriptor(int descriptor) noexcept
    : descriptor_(descriptor) {
}

OwnedFileDescriptor::~OwnedFileDescriptor() noexcept {
    reset();
}

OwnedFileDescriptor::OwnedFileDescriptor(OwnedFileDescriptor&& other) noexcept
    : descriptor_(other.release()) {
}

OwnedFileDescriptor& OwnedFileDescriptor::operator=(OwnedFileDescriptor&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int OwnedFileDescriptor::get() const noexcept {
    return descriptor_;
}

bool OwnedFileDescriptor::valid() const noexcept {
    return descriptor_ >= 0;
}

int OwnedFileDescriptor::release() noexcept {
    return std::exchange(descriptor_, -1);
}

void OwnedFileDescriptor::reset(int descriptor) noexcept {
    const int previous = std::exchange(descriptor_, descriptor);
    if (previous >= 0) {
        ::close(previous);
    }
}

} // namespace btrfsbackup::platform::linux
