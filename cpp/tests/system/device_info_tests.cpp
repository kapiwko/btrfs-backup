#include <filesystem>
#include <string>

#include <btrfsbackup/system/device_info.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_mapper_path() {
    test_helpers::expect_eq("default mapper path", btrfsbackup::mapper_path("backupdisk").string(), "/dev/mapper/backupdisk");
    test_helpers::expect_eq("custom mapper path", btrfsbackup::mapper_path("backupdisk", "/tmp/dev/mapper").string(), "/tmp/dev/mapper/backupdisk");
}

void test_canonical_device() {
    fs::path root = test_helpers::test_root("device-info", "canonical");
    fs::path target = root / "target";
    fs::path link = root / "link";
    test_helpers::write_file(target, "device\n");
    fs::create_symlink(target, link);

    test_helpers::expect_eq("canonical symlink", btrfsbackup::canonical_device(link).string(), target.string());
    test_helpers::expect_true("canonical missing", btrfsbackup::canonical_device(root / "missing").empty(), "missing device should produce empty path");

    fs::remove_all(root);
}

void test_strip_subvolume_suffix() {
    test_helpers::expect_eq("strip suffix", btrfsbackup::strip_subvolume_suffix("/dev/mapper/backup[/subvol]"), "/dev/mapper/backup");
    test_helpers::expect_eq("strip no suffix", btrfsbackup::strip_subvolume_suffix("/dev/mapper/backup"), "/dev/mapper/backup");
}

} // namespace

int main() {
    test_mapper_path();
    test_canonical_device();
    test_strip_subvolume_suffix();

    return test_helpers::finish("device info tests");
}
