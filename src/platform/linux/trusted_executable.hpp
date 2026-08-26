// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/trusted_executable.hpp>
#include <platform/linux/safe_directory_root.hpp>

namespace btrfsbackup {

SafeDirectoryHandle open_trusted_executable(
    const SafeDirectoryRoot& trusted_root,
    const std::filesystem::path& program,
    const TrustedExecutablePolicy& policy = {}
);

class PosixTrustedExecutableResolver final : public ITrustedExecutableResolver {
  public:
    PosixTrustedExecutableResolver(
        std::filesystem::path trusted_root,
        TrustedExecutablePolicy policy = {}
    );

    [[nodiscard]] std::unique_ptr<ITrustedExecutable> resolve(
        const std::filesystem::path& program
    ) const override;

  private:
    std::filesystem::path trusted_root_;
    TrustedExecutablePolicy policy_;
};

} // namespace btrfsbackup
