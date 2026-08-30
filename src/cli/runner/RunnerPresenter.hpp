// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iosfwd>

#include <backup/model/BackupExecution.hpp>

namespace btrfsbackup::cli::runner {

void print_runner_usage(std::ostream& output);
int present_runner_plan(const btrfsbackup::backup::BackupRunPlan& plan, std::ostream& output);
int present_runner_execution(const btrfsbackup::backup::BackupExecutionResult& result, std::ostream& output);
int present_runner_cancellation(const btrfsbackup::backup::CancelBackupResult& result, std::ostream& output);

} // namespace btrfsbackup::cli::runner
