// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/CancellationRequestStore.hpp>
#include <state/PersistentDocumentOperations.hpp>

namespace btrfsbackup::state {

class FileActiveRunRegistration final : public btrfsbackup::backup::IActiveRunRegistration {
  public:
    FileActiveRunRegistration(
        IDurableDocumentRemover& files,
        std::filesystem::path profile_state_dir,
        RunId run_id
    );
    ~FileActiveRunRegistration() override;

    std::optional<std::string> close() override;

  private:
    IDurableDocumentRemover& files_;
    std::filesystem::path profile_state_dir_;
    RunId run_id_;
    bool closed_ = false;
};

} // namespace btrfsbackup::state
