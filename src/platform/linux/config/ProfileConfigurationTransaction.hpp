// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <core/Errors.hpp>
#include <config/ProfileArtifactRenderer.hpp>

namespace btrfsbackup::platform::linux::config {

// Filesystem transaction used by the Linux profile installer.

struct RollbackError {
    std::string operation;
    std::filesystem::path path;
    std::string message;
};

struct RollbackResult {
    bool complete = true;
    std::vector<RollbackError> errors;
};

struct ConfigurationSaveError : CodedValidationError {
    ConfigurationSaveError(std::string message, RollbackResult rollback_result);

    RollbackResult rollback_result;
};

class ProfileConfigurationTransaction {
  public:
    explicit ProfileConfigurationTransaction(const btrfsbackup::config::RenderedProfileArtifacts& rendered);

    void stage();
    [[nodiscard]] std::filesystem::path staged_path(btrfsbackup::config::ProfileArtifactKind kind) const;
    void publish_configuration();
    void publish_public_marker();
    [[nodiscard]] RollbackResult rollback() noexcept;
    void finish() noexcept;

  private:
    struct TransactionArtifact {
        btrfsbackup::config::ProfileArtifactKind kind;
        std::filesystem::path destination;
        std::filesystem::path staged;
        std::filesystem::path previous;
        std::string content;
        std::filesystem::perms permissions;
        btrfsbackup::config::ProfileArtifactOperation operation;
        bool had_previous = false;
        bool published = false;
    };

    void publish(TransactionArtifact& artifact);
    void rollback_artifact(TransactionArtifact& artifact, RollbackResult& result) noexcept;
    void remove_published_artifact(TransactionArtifact& artifact, RollbackResult& result) noexcept;
    [[nodiscard]] bool restore_previous_artifact(TransactionArtifact& artifact, RollbackResult& result) noexcept;
    void sync_rollback_directory(const TransactionArtifact& artifact, RollbackResult& result) noexcept;
    void remove_transaction_files(
        const TransactionArtifact& artifact,
        bool restored_previous,
        RollbackResult& result
    ) noexcept;
    [[nodiscard]] TransactionArtifact& artifact(btrfsbackup::config::ProfileArtifactKind kind);
    [[nodiscard]] const TransactionArtifact& artifact(btrfsbackup::config::ProfileArtifactKind kind) const;

    btrfsbackup::config::ConfigurationGeneration generation_;
    std::vector<TransactionArtifact> artifacts_;
};

} // namespace btrfsbackup::platform::linux::config
