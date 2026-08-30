// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <daemon/AuthorizedOperationCommand.hpp>

namespace btrfsbackup::daemon {

class OperationEnvironmentFile {
  public:
    OperationEnvironmentFile(
        const std::filesystem::path& root,
        const AuthorizedOperationContext& context
    );
    ~OperationEnvironmentFile() noexcept;

    OperationEnvironmentFile(const OperationEnvironmentFile&) = delete;
    OperationEnvironmentFile& operator=(const OperationEnvironmentFile&) = delete;
    OperationEnvironmentFile(OperationEnvironmentFile&&) = delete;
    OperationEnvironmentFile& operator=(OperationEnvironmentFile&&) = delete;

  private:
    std::filesystem::path path_;
};

} // namespace btrfsbackup::daemon
