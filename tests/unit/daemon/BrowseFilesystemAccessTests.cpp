// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseFilesystemAccess.hpp>

#include <fcntl.h>
#include <acl/libacl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <functional>
#include <string>

#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

using btrfsbackup::daemon::control::BrowseFilesystemAccess;
using btrfsbackup::daemon::control::BrowseAccessIdentity;

void expect_rejected(const std::string& name, const std::function<void()>& operation) {
    try {
        operation();
        test_helpers::fail(name, "unsafe or unsupported entry was accepted");
    } catch (const std::exception&) {
    }
}

void test_validated_access_and_types() {
    const fs::path root = test_helpers::test_root("browse-filesystem", "access");
    const fs::path outside = test_helpers::test_root("browse-filesystem", "outside");
    fs::create_directories(root / "directory");
    test_helpers::write_file(root / "directory/file.txt", "data");
    test_helpers::write_file(outside / "secret.txt", "secret");
    fs::create_symlink(root / "directory/file.txt", root / "file-link");
    fs::create_directory_symlink(outside, root / "directory-link");
    test_helpers::expect_true("create special entry", mkfifo((root / "pipe").c_str(), 0600) == 0, "cannot create FIFO fixture");

    BrowseFilesystemAccess access;
    const auto file = access.inspect_entry(root, "directory/file.txt");
    test_helpers::expect_true(
        "regular metadata",
        !file.directory && file.size == 4 && file.name == "file.txt",
        "regular file metadata changed"
    );
    auto descriptor = access.open_file(root, "directory/file.txt");
    char buffer[4]{};
    test_helpers::expect_true(
        "regular descriptor",
        descriptor.valid() && ::read(descriptor.get(), buffer, sizeof(buffer)) == 4 && std::string(buffer, 4) == "data",
        "regular file was not opened read-only"
    );
    test_helpers::expect_true(
        "regular entry descriptor",
        access.open_entry(root, "directory/file.txt").valid(),
        "regular restore entry did not open"
    );
    test_helpers::expect_true(
        "directory entry descriptor",
        access.open_entry(root, "directory").valid(),
        "directory restore entry did not open"
    );
    test_helpers::expect_true("root descriptor", access.open_root(root).valid(), "browse root did not open");

    expect_rejected("parent traversal", [&] { (void)access.inspect_entry(root, "../outside/secret.txt"); });
    expect_rejected("absolute traversal", [&] { (void)access.inspect_entry(root, outside / "secret.txt"); });
    expect_rejected("final symlink", [&] { (void)access.inspect_entry(root, "file-link"); });
    expect_rejected("intermediate symlink", [&] { (void)access.inspect_entry(root, "directory-link/secret.txt"); });
    expect_rejected("special entry inspection", [&] { (void)access.inspect_entry(root, "pipe"); });
    expect_rejected("directory as file", [&] { (void)access.open_file(root, "directory"); });

    fs::remove_all(root);
    fs::remove_all(outside);
}

void test_listing_filters_and_pages() {
    const fs::path root = test_helpers::test_root("browse-filesystem", "pages");
    for (const std::string& name : {"delta", "alpha", "charlie", "bravo"})
        test_helpers::write_file(root / name, name);
    fs::create_directories(root / ".incoming");
    fs::create_symlink(root / "alpha", root / "link");
    test_helpers::expect_true("create special entry", mkfifo((root / "pipe").c_str(), 0600) == 0, "cannot create FIFO fixture");

    BrowseFilesystemAccess access;
    const auto entries = access.list_directory(root, ".", 10);
    std::vector<std::string> names;
    for (const auto& entry : entries)
        names.push_back(entry.name);
    std::ranges::sort(names);
    test_helpers::expect_true(
        "filtered listing",
        names == std::vector<std::string>{"alpha", "bravo", "charlie", "delta"},
        "listing exposed internal, symlink or special entries"
    );
    const auto first = access.list_directory_page(root, ".", "", 2);
    const auto second = access.list_directory_page(root, ".", first.continuation_token, 2);
    test_helpers::expect_true(
        "first page",
        first.entries.size() == 2 && first.entries[0].name == "alpha" && first.entries[1].name == "bravo" &&
            first.continuation_token == "bravo",
        "first page is not bounded and name-sorted"
    );
    test_helpers::expect_true(
        "second page",
        second.entries.size() == 2 && second.entries[0].name == "charlie" && second.entries[1].name == "delta" &&
            second.continuation_token.empty(),
        "continuation did not resume after the prior page"
    );
    expect_rejected("legacy entry limit", [&] { (void)access.list_directory(root, ".", 1); });
    fs::remove_all(root);
}

bool set_acl(const fs::path& path, const std::string& text) {
    acl_t acl = acl_from_text(text.c_str());
    if (acl == nullptr) {
        test_helpers::fail("parse ACL fixture", "cannot parse ACL fixture");
        return false;
    }
    const int result = acl_set_file(path.c_str(), ACL_TYPE_ACCESS, acl);
    const int error = errno;
    acl_free(acl);
    if (result == 0)
        return true;
    if (error == EPERM || error == ENOTSUP || error == EOPNOTSUPP)
        return false;
    test_helpers::fail(
        "set ACL fixture",
        "cannot set ACL fixture on " + path.string() + ": " + std::strerror(error)
    );
    return false;
}

void test_stored_permissions_and_acl_are_enforced() {
    const fs::path root = test_helpers::test_root("browse-filesystem", "permissions");
    test_helpers::write_file(root / "allowed.txt", "allowed");
    test_helpers::write_file(root / "denied.txt", "denied");
    fs::permissions(root / "allowed.txt", fs::perms::owner_read);
    fs::permissions(root / "denied.txt", fs::perms::none);
    const BrowseAccessIdentity owner{
        .uid = static_cast<std::uint32_t>(getuid()),
        .groups = {static_cast<std::uint32_t>(getgid())},
    };
    BrowseFilesystemAccess access;
    test_helpers::expect_true(
        "owner read permission",
        access.open_file(root, "allowed.txt", &owner).valid(),
        "stored owner read permission was rejected"
    );
    expect_rejected("owner read denied", [&] { (void)access.open_file(root, "denied.txt", &owner); });

    const std::string acl_group = std::to_string(getgid());
    const bool acl_fixtures_ready = set_acl(root, "u::rwx,g::---,g:" + acl_group + ":r-x,m::r-x,o::---") &&
        set_acl(root / "allowed.txt", "u::r--,g::---,g:" + acl_group + ":r--,m::r--,o::---") &&
        set_acl(root / "denied.txt", "u::r--,g::---,g:" + acl_group + ":r--,m::---,o::---");
    if (!acl_fixtures_ready) {
        fs::remove_all(root);
        return;
    }
    const BrowseAccessIdentity named_group{
        .uid = 12345,
        .groups = {static_cast<std::uint32_t>(getgid())},
    };
    test_helpers::expect_true(
        "named ACL read permission",
        access.open_file(root, "allowed.txt", &named_group).valid(),
        "named POSIX ACL group permission was rejected"
    );
    expect_rejected("ACL mask denied", [&] { (void)access.open_file(root, "denied.txt", &named_group); });
    fs::remove_all(root);
}

} // namespace

int main() {
    test_validated_access_and_types();
    test_listing_filters_and_pages();
    test_stored_permissions_and_acl_are_enforced();
    return test_helpers::finish("browse filesystem access tests");
}
