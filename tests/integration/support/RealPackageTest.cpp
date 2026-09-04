// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RealPackageTest.hpp"

#include "IntegrationTestProcess.hpp"

#include <algorithm>
#include <chrono>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace btrfsbackup::integration {
namespace fs = std::filesystem;

namespace {

constexpr auto command_timeout = std::chrono::seconds(120);

[[nodiscard]] CommandResult command(std::vector<std::string> arguments) {
    return run_test_process(std::move(arguments), command_timeout);
}

void require_success(std::vector<std::string> arguments, std::string_view operation) {
    const auto result = command(std::move(arguments));
    if (result.status != 0)
        throw std::runtime_error(std::string(operation) + " failed: " + command_diagnostic(result));
}

} // namespace

RealPackageTest::RealPackageTest(fs::path package_directory)
    : package_directory_(fs::canonical(package_directory)) {}

fs::path RealPackageTest::base_package() const {
    std::vector<fs::path> packages;
    for (const auto& entry : fs::directory_iterator(package_directory_)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("btrfs-backup-") &&
            name.ends_with(".pkg.tar.zst") && !name.starts_with("btrfs-backup-kde-"))
            packages.push_back(entry.path());
    }
    if (packages.size() != 1)
        throw std::runtime_error("expected exactly one Arch base package, found " +
                                 std::to_string(packages.size()));
    return packages.front();
}

void RealPackageTest::install_and_verify() const {
    const auto package = base_package();
    const auto metadata = command({"tar", "--zstd", "-xOf", package.string(), ".PKGINFO"});
    if (metadata.status != 0)
        throw std::runtime_error("cannot inspect Arch base package: " + command_diagnostic(metadata));
    const std::regex kde_dependency(
        R"((^|\n)depend = (extra-cmake-modules|ki18n|kirigami|kpackage|kservice|libplasma|qt6-[^ <>=]+))"
    );
    if (std::regex_search(metadata.output, kde_dependency))
        throw std::runtime_error("base package has a KDE or Qt runtime dependency");

    require_success({"pacman", "-U", "--noconfirm", package.string()}, "install Arch base package");
    if (command({"pacman", "-Q", "btrfs-backup-kde"}).status == 0)
        throw std::runtime_error("KDE package was installed with the base package");

    const std::vector<fs::path> commands{
        "/usr/bin/btrfs-backup",
        "/usr/bin/btrfs-backupctl",
        "/usr/bin/btrfs-backupd",
    };
    for (const auto& executable : commands) {
        if (!fs::is_regular_file(executable) ||
            (fs::status(executable).permissions() & fs::perms::owner_exec) == fs::perms::none)
            throw std::runtime_error("base package did not install executable " + executable.string());
        require_success({executable.string(), "--help"}, "run installed " + executable.filename().string());
    }
    if (!fs::is_regular_file("/usr/bin/pkaction"))
        throw std::runtime_error("base package did not install its polkit runtime dependency");

    std::vector<std::string> ldd{"ldd"};
    std::ranges::transform(commands, std::back_inserter(ldd), [](const fs::path& path) {
        return path.string();
    });
    const auto linkage = command(std::move(ldd));
    if (linkage.status != 0)
        throw std::runtime_error("cannot inspect installed command linkage: " + command_diagnostic(linkage));
    if (std::regex_search(linkage.output, std::regex(R"(lib(Qt6|KF6|Plasma))")))
        throw std::runtime_error("base commands link to a KDE or Qt runtime library");
}

} // namespace btrfsbackup::integration
