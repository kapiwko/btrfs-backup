// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

#include <core/Cancellation.hpp>

namespace btrfsbackup::restore {

struct RestoreStatistics {
    std::uint64_t files = 0;
    std::uint64_t directories = 0;
    std::uint64_t bytes = 0;
};

struct RestoreProgress {
    RestoreStatistics statistics;
    std::filesystem::path current_path;
};

using RestoreProgressSink = std::function<void(const RestoreProgress&)>;

class IRestoreOperations {
  public:
    virtual ~IRestoreOperations() = default;

    [[nodiscard]] virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual void ensure_sufficient_space(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        CancellationToken& cancellation
    ) const = 0;
    virtual void prepare_copy_root(
        const std::filesystem::path& source,
        const std::filesystem::path& path
    ) = 0;
    virtual void create_subvolume_root(const std::filesystem::path& path) = 0;
    virtual RestoreStatistics copy_and_verify(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_root,
        CancellationToken& cancellation,
        const RestoreProgressSink& progress = {}
    ) = 0;
    virtual void move(const std::filesystem::path& source, const std::filesystem::path& destination) = 0;
    virtual void remove_owned_tree(const std::filesystem::path& path) = 0;
};

} // namespace btrfsbackup::restore
