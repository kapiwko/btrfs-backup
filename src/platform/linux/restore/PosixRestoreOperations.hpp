// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <restore/RestoreOperations.hpp>

namespace btrfsbackup::platform::linux::restore {

class PosixRestoreOperations final : public btrfsbackup::restore::IRestoreOperations {
  public:
    [[nodiscard]] bool exists(const std::filesystem::path& path) const override;
    void prepare_copy_root(
        const std::filesystem::path& source,
        const std::filesystem::path& path
    ) override;
    void create_subvolume_root(const std::filesystem::path& path) override;
    btrfsbackup::restore::RestoreStatistics copy_and_verify(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_root,
        CancellationToken& cancellation,
        const btrfsbackup::restore::RestoreProgressSink& progress = {}
    ) override;
    void move(const std::filesystem::path& source, const std::filesystem::path& destination) override;
    void remove_owned_tree(const std::filesystem::path& path) override;
};

} // namespace btrfsbackup::platform::linux::restore
