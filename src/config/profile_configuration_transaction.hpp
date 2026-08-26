// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <config/errors.hpp>
#include <config/profile_artifact_renderer.hpp>

namespace btrfsbackup {

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
    explicit ProfileConfigurationTransaction(const RenderedProfileArtifacts& rendered);

    void stage();
    [[nodiscard]] std::filesystem::path staged_path(ProfileArtifactKind kind) const;
    void publish_configuration();
    void publish_public_marker();
    [[nodiscard]] RollbackResult rollback() noexcept;
    void finish() noexcept;

  private:
    struct TransactionArtifact {
        ProfileArtifactKind kind;
        std::filesystem::path destination;
        std::filesystem::path staged;
        std::filesystem::path previous;
        std::string content;
        mode_t mode;
        bool had_previous = false;
        bool published = false;
    };

    void publish(TransactionArtifact& artifact);
    [[nodiscard]] TransactionArtifact& artifact(ProfileArtifactKind kind);
    [[nodiscard]] const TransactionArtifact& artifact(ProfileArtifactKind kind) const;

    std::string generation_;
    std::vector<TransactionArtifact> artifacts_;
};

} // namespace btrfsbackup
