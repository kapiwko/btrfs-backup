// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/json/Json.hpp>
#include <state/persistence/PersistentDocumentOperations.hpp>
#include <state/model/RunStatus.hpp>

namespace btrfsbackup::state {

btrfsbackup::config::json::Json build_status_json(const RunStatus& status);
std::string dump_status_json(const RunStatus& status);
btrfsbackup::config::json::Json build_public_status_json(const RunStatus& status);
std::string dump_public_status_json(const RunStatus& status);

void write_current_status(
    IAtomicDocumentWriter& files,
    const std::filesystem::path& status_root,
    const RunStatus& status
);

} // namespace btrfsbackup::state
