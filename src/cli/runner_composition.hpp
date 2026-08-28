// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <core/cancellation.hpp>

namespace btrfsbackup::backup {
class BackupService;
} // namespace btrfsbackup::backup

namespace btrfsbackup::cli {

struct RunnerOptions;

class RunnerComposition {
  public:
    RunnerComposition(
        const std::filesystem::path& config_root,
        const RunnerOptions& options,
        CancellationToken& cancellation
    );
    ~RunnerComposition();

    RunnerComposition(const RunnerComposition&) = delete;
    RunnerComposition& operator=(const RunnerComposition&) = delete;

    backup::BackupService& service();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::cli
