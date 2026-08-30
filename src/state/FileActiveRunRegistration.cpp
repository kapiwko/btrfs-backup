// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/FileActiveRunRegistration.hpp>

#include <exception>
#include <type_traits>

static_assert(std::is_nothrow_destructible_v<btrfsbackup::state::FileActiveRunRegistration>);
#include <iostream>
#include <utility>

#include <state/RunState.hpp>

namespace btrfsbackup::state {

FileActiveRunRegistration::FileActiveRunRegistration(
    IDurableDocumentRemover& files,
    std::filesystem::path profile_state_dir,
    RunId run_id
)
    : files_(files), profile_state_dir_(std::move(profile_state_dir)), run_id_(std::move(run_id)) {
}

FileActiveRunRegistration::~FileActiveRunRegistration() {
    try {
        if (std::optional<std::string> diagnostic = close()) {
            std::clog << "btrfs-backup: active run cleanup failed: " << *diagnostic << '\n';
        }
    } catch (const std::exception& error) {
        std::clog << "btrfs-backup: active run cleanup failed: " << error.what() << '\n';
    } catch (...) {
        std::clog << "btrfs-backup: active run cleanup failed with an unknown error\n";
    }
}

std::optional<std::string> FileActiveRunRegistration::close() {
    if (closed_) {
        return std::nullopt;
    }
    closed_ = true;
    try {
        btrfsbackup::state::clear_active_run(files_, profile_state_dir_, run_id_);
        return std::nullopt;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown active run cleanup failure";
    }
}

} // namespace btrfsbackup::state
