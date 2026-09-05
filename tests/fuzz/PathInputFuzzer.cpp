// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

#include <config/domain/OperationPath.hpp>
#include <config/domain/RepositoryPath.hpp>
#include <config/domain/Validation.hpp>
#include <restore/RepositoryCatalog.hpp>

namespace {

void require_absolute(const std::filesystem::path& path) {
    if (!path.is_absolute()) {
        std::abort();
    }
}

template <typename PathType>
void fuzz_absolute_path(const std::string& input) {
    try {
        const PathType path{input};
        require_absolute(path.value());
    } catch (const std::exception&) {
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string input;
    if (size != 0) {
        input.assign(reinterpret_cast<const char*>(data), size);
    }

    fuzz_absolute_path<btrfsbackup::config::RemoteSnapshotRoot>(input);
    fuzz_absolute_path<btrfsbackup::config::IncomingRoot>(input);
    fuzz_absolute_path<btrfsbackup::config::TargetMountPoint>(input);
    fuzz_absolute_path<btrfsbackup::config::SourceSubvolumePath>(input);
    fuzz_absolute_path<btrfsbackup::config::LocalSnapshotRoot>(input);
    fuzz_absolute_path<btrfsbackup::config::KeyFilePath>(input);
    fuzz_absolute_path<btrfsbackup::config::HookProgramPath>(input);

    try {
        const btrfsbackup::config::TargetDevicePath path{input};
        if (!btrfsbackup::config::path_is_within(path.value(), "/dev")) {
            std::abort();
        }
    } catch (const std::exception&) {
    }

    try {
        const btrfsbackup::config::SafeRelativePath path{input};
        if (path.value().empty() || path.value().is_absolute() || path.value().lexically_normal() != path.value()) {
            std::abort();
        }
        for (const auto& component : path.value()) {
            if (component.empty() || component == "." || component == "..") {
                std::abort();
            }
        }
    } catch (const std::exception&) {
    }

    try {
        const btrfsbackup::restore::RelativeRestorePath path{input};
        if (path.value().is_absolute() || path.value().lexically_normal() != path.value()) {
            std::abort();
        }
        for (const auto& component : path.value()) {
            if (component == "..") {
                std::abort();
            }
        }
    } catch (const std::exception&) {
    }

    const std::size_t midpoint = size / 2;
    const std::filesystem::path candidate{input.substr(0, midpoint)};
    const std::filesystem::path base{input.substr(midpoint)};
    (void)btrfsbackup::config::path_is_within(candidate, base);
    return 0;
}
