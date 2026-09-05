// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseFilesystemAccess.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <string>

#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

using btrfsbackup::daemon::control::BrowseFilesystemAccess;

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

} // namespace

int main() {
    test_validated_access_and_types();
    test_listing_filters_and_pages();
    return test_helpers::finish("browse filesystem access tests");
}
