#include <filesystem>
#include <string>

#include <platform/linux/filesystem.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

int main() {
    fs::path root = test_helpers::test_root("filesystem", "posix-filesystem");
    btrfsbackup::PosixFileSystem filesystem;
    fs::path directory = root / "dir";
    filesystem.create_directories(directory);
    test_helpers::expect_true("fs directory", filesystem.is_directory(directory), "directory should exist");

    fs::path source = directory / "source";
    fs::path target = directory / "target";
    test_helpers::write_file(source, "content");
    test_helpers::expect_true("fs file exists", filesystem.exists(source), "source file should exist");
    filesystem.rename_path(source, target);
    test_helpers::expect_true("fs renamed target", filesystem.exists(target), "target file should exist");
    test_helpers::expect_true("fs renamed source", !filesystem.exists(source), "source file should be gone");
    filesystem.remove_file(target);
    test_helpers::expect_true("fs removed", !filesystem.exists(target), "target file should be removed");

    fs::remove_all(root);
    return test_helpers::finish("filesystem tests");
}
