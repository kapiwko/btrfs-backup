// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/CancellationRequestStore.hpp>
#include <state/persistence/PersistentDocumentOperations.hpp>

namespace btrfsbackup::state {

class FileActiveRunRegistration final : public btrfsbackup::backup::IActiveRunRegistration {
  public:
    FileActiveRunRegistration(
        IDurableDocumentRemover& files,
        std::filesystem::path profile_state_dir,
        RunId run_id
    );
    FileActiveRunRegistration(const FileActiveRunRegistration&) = delete;
    FileActiveRunRegistration& operator=(const FileActiveRunRegistration&) = delete;
    FileActiveRunRegistration(FileActiveRunRegistration&&) = delete;
    FileActiveRunRegistration& operator=(FileActiveRunRegistration&&) = delete;
    ~FileActiveRunRegistration() noexcept override;

    [[nodiscard]] const std::optional<btrfsbackup::backup::CleanupDiagnostic>& close() noexcept override;

  private:
    IDurableDocumentRemover& files_;
    std::filesystem::path profile_state_dir_;
    RunId run_id_;
    bool closed_ = false;
    std::optional<btrfsbackup::backup::CleanupDiagnostic> close_diagnostic_;
};

} // namespace btrfsbackup::state
