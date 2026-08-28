// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/trusted_executable.hpp>
#include <platform/linux/safe_directory_root.hpp>

namespace btrfsbackup::platform::linux {

SafeDirectoryHandle open_trusted_executable(
    const SafeDirectoryRoot& trusted_root,
    const std::filesystem::path& program,
    const btrfsbackup::backup::TrustedExecutablePolicy& policy = {}
);

class PosixTrustedExecutableResolver final : public btrfsbackup::backup::ITrustedExecutableResolver {
  public:
    PosixTrustedExecutableResolver(
        std::filesystem::path trusted_root,
        btrfsbackup::backup::TrustedExecutablePolicy policy = {}
    );

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ITrustedExecutable> resolve(
        const std::filesystem::path& program
    ) const override;

  private:
    std::filesystem::path trusted_root_;
    btrfsbackup::backup::TrustedExecutablePolicy policy_;
};

} // namespace btrfsbackup::platform::linux
