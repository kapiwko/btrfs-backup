// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/cancellation/FileActiveRunRegistration.hpp>

#include <exception>
#include <iostream>
#include <type_traits>
#include <utility>

#include <state/query/RunState.hpp>

static_assert(std::is_nothrow_destructible_v<btrfsbackup::state::FileActiveRunRegistration>);

namespace btrfsbackup::state {

FileActiveRunRegistration::FileActiveRunRegistration(
    IDurableDocumentRemover& files,
    std::filesystem::path profile_state_dir,
    RunId run_id
)
    : files_(files), profile_state_dir_(std::move(profile_state_dir)), run_id_(std::move(run_id)) {
}

FileActiveRunRegistration::~FileActiveRunRegistration() noexcept {
    if (const auto& diagnostic = close()) {
        std::clog << "btrfs-backup: active run cleanup failed: " << diagnostic->message << '\n';
    }
}

const std::optional<btrfsbackup::backup::CleanupDiagnostic>& FileActiveRunRegistration::close() noexcept {
    if (closed_) {
        return close_diagnostic_;
    }
    closed_ = true;
    try {
        btrfsbackup::state::clear_active_run(files_, profile_state_dir_, run_id_);
    } catch (const std::exception& error) {
        close_diagnostic_ = btrfsbackup::backup::CleanupDiagnostic{error.what()};
    } catch (...) {
        close_diagnostic_ = btrfsbackup::backup::CleanupDiagnostic{"unknown active run cleanup failure"};
    }
    return close_diagnostic_;
}

} // namespace btrfsbackup::state
