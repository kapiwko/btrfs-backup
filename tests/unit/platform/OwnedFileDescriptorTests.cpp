// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <type_traits>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <platform/linux/OwnedFileDescriptor.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::platform::linux::OwnedFileDescriptor;

static_assert(std::is_nothrow_move_constructible_v<OwnedFileDescriptor>);
static_assert(std::is_nothrow_move_assignable_v<OwnedFileDescriptor>);

void expect_closed(const std::string& name, int descriptor) {
    errno = 0;
    test_helpers::expect_true(name, ::fcntl(descriptor, F_GETFD) == -1 && errno == EBADF, "descriptor should be closed");
}

void test_destructor_closes_descriptor() {
    int descriptors[2];
    test_helpers::expect_true("create destructor pipe", ::pipe2(descriptors, O_CLOEXEC) == 0, "pipe2 should succeed");
    {
        OwnedFileDescriptor descriptor(descriptors[0]);
        test_helpers::expect_true("owned descriptor valid", descriptor.valid(), "owned descriptor should be valid");
        test_helpers::expect_true("owned descriptor value", descriptor.get() == descriptors[0], "owned descriptor should retain its value");
    }
    expect_closed("destructor closes descriptor", descriptors[0]);
    ::close(descriptors[1]);
}

void test_move_transfers_ownership() {
    int descriptors[2];
    test_helpers::expect_true("create move pipe", ::pipe2(descriptors, O_CLOEXEC) == 0, "pipe2 should succeed");
    {
        OwnedFileDescriptor source(descriptors[0]);
        OwnedFileDescriptor destination(std::move(source));
        test_helpers::expect_true("moved source invalid", !source.valid(), "moved source should be invalid");
        test_helpers::expect_true("moved destination valid", destination.get() == descriptors[0], "destination should own descriptor");
    }
    expect_closed("moved descriptor closed", descriptors[0]);
    ::close(descriptors[1]);
}

void test_reset_closes_previous_descriptor() {
    int first[2];
    int second[2];
    test_helpers::expect_true("create first reset pipe", ::pipe2(first, O_CLOEXEC) == 0, "pipe2 should succeed");
    test_helpers::expect_true("create second reset pipe", ::pipe2(second, O_CLOEXEC) == 0, "pipe2 should succeed");
    {
        OwnedFileDescriptor descriptor(first[0]);
        descriptor.reset(second[0]);
        expect_closed("reset closes previous descriptor", first[0]);
        test_helpers::expect_true("reset owns replacement", descriptor.get() == second[0], "descriptor should own replacement");
    }
    expect_closed("replacement closed by destructor", second[0]);
    ::close(first[1]);
    ::close(second[1]);
}

void test_release_returns_ownership() {
    int descriptors[2];
    test_helpers::expect_true("create release pipe", ::pipe2(descriptors, O_CLOEXEC) == 0, "pipe2 should succeed");
    int released = -1;
    {
        OwnedFileDescriptor descriptor(descriptors[0]);
        released = descriptor.release();
        test_helpers::expect_true("released owner invalid", !descriptor.valid(), "owner should be invalid after release");
    }
    test_helpers::expect_true("released descriptor remains open", ::fcntl(released, F_GETFD) != -1, "released descriptor should remain open");
    ::close(released);
    ::close(descriptors[1]);
}

} // namespace

int main() {
    test_destructor_closes_descriptor();
    test_move_transfers_ownership();
    test_reset_closes_previous_descriptor();
    test_release_returns_ownership();

    return test_helpers::finish("owned file descriptor tests");
}
