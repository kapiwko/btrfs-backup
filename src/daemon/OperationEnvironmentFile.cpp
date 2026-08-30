// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/OperationEnvironmentFile.hpp>

#include <string>
#include <system_error>
#include <type_traits>

#include <platform/linux/FileIo.hpp>

static_assert(std::is_nothrow_destructible_v<btrfsbackup::daemon::OperationEnvironmentFile>);

namespace btrfsbackup::daemon {

OperationEnvironmentFile::OperationEnvironmentFile(
    const std::filesystem::path& root,
    const AuthorizedOperationContext& context
) : path_(root / (std::string(context.operation_id.value()) + ".env")) {
    btrfsbackup::platform::linux::atomic_write(
        path_,
        authorized_operation_environment(context),
        0600
    );
}

OperationEnvironmentFile::~OperationEnvironmentFile() noexcept {
    std::error_code error;
    std::filesystem::remove(path_, error);
}

} // namespace btrfsbackup::daemon
