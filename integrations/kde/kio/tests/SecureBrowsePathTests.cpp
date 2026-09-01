// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SecureBrowsePath.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace fs = std::filesystem;
using btrfsbackup::kde::kio::BrowseDirectoryLimitError;
using btrfsbackup::kde::kio::list_browse_directory;
using btrfsbackup::kde::kio::open_browse_directory;
using btrfsbackup::kde::kio::open_browse_regular_file;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "not ok - " << message << '\n';
        ++failures;
    }
}

template <typename Operation>
void expect_rejected(const char* message, Operation operation) {
    try {
        operation();
        expect(false, message);
    } catch (...) {
    }
}

fs::path test_root(const char* name) {
    const fs::path root = fs::temp_directory_path() /
        ("btrfsbackup-kio-secure-path-" + std::string(name) + "-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::string read_descriptor(int descriptor) {
    std::string result;
    char buffer[64];
    while (true) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count < 0)
            throw std::runtime_error("cannot read test descriptor");
        if (count == 0)
            return result;
        result.append(buffer, static_cast<std::size_t>(count));
    }
}

void test_rejects_traversal_and_symlinks() {
    const fs::path root = test_root("traversal");
    const fs::path outside = root.parent_path() / (root.filename().string() + "-outside");
    write_file(root / "snapshot" / "file.txt", "inside");
    write_file(outside / "secret.txt", "outside");
    fs::create_symlink(outside / "secret.txt", root / "snapshot" / "file-link");
    fs::create_directory_symlink(outside, root / "snapshot" / "directory-link");

    expect_rejected("parent traversal was accepted", [&] {
        (void)open_browse_regular_file(root, "../" / outside.filename() / "secret.txt");
    });
    expect_rejected("final symlink was accepted", [&] {
        (void)open_browse_regular_file(root, "snapshot/file-link");
    });
    expect_rejected("intermediate symlink was accepted", [&] {
        (void)open_browse_regular_file(root, "snapshot/directory-link/secret.txt");
    });

    fs::remove_all(root);
    fs::remove_all(outside);
}

void test_open_descriptor_survives_path_reuse_and_session_loss() {
    const fs::path root = test_root("reuse");
    write_file(root / "snapshot" / "file.txt", "original");
    const auto session_root = open_browse_directory(root, {});
    const fs::path detached = root.parent_path() / (root.filename().string() + "-detached");
    fs::rename(root, detached);
    write_file(root / "snapshot" / "file.txt", "replacement");
    auto file = open_browse_regular_file(session_root.descriptor(), "snapshot/file.txt");
    fs::remove_all(root);
    fs::remove_all(detached);

    expect(read_descriptor(file.descriptor()) == "original", "session descriptor followed a reused path");
}

void test_directory_filter_limit_and_read_only_copy() {
    const fs::path root = test_root("directory");
    write_file(root / "snapshot" / "a.txt", "alpha");
    write_file(root / "snapshot" / "b.txt", "beta");
    write_file(root / "snapshot" / ".incoming" / "hidden", "hidden");
    fs::create_directory(root / "snapshot" / "subdir");
    fs::create_symlink(root / "snapshot" / "a.txt", root / "snapshot" / "link");
    mkfifo((root / "snapshot" / "pipe").c_str(), 0600);

    const auto directory = open_browse_directory(root, "snapshot");
    const auto entries = list_browse_directory(directory.descriptor(), 10);
    std::set<std::string> names;
    for (const auto& entry : entries)
        names.insert(entry.name);
    expect(names == std::set<std::string>{"a.txt", "b.txt", "subdir"}, "directory listing exposed an unsafe entry");
    expect_rejected("large directory limit was not enforced", [&] {
        (void)list_browse_directory(directory.descriptor(), 1);
    });

    auto source = open_browse_regular_file(root, "snapshot/a.txt");
    const int flags = fcntl(source.descriptor(), F_GETFL);
    expect((flags & O_ACCMODE) == O_RDONLY, "browse file was not opened read-only");
    errno = 0;
    expect(write(source.descriptor(), "x", 1) < 0 && errno == EBADF, "browse descriptor accepted a write");
    expect(read_descriptor(source.descriptor()) == "alpha", "file copy read returned incorrect data");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_rejects_traversal_and_symlinks();
    test_open_descriptor_survives_path_reuse_and_session_loss();
    test_directory_filter_limit_and_read_only_copy();
    if (failures == 0)
        std::cout << "ok - secure KIO browse path tests\n";
    return failures == 0 ? 0 : 1;
}
