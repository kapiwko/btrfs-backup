// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/ITargetStorageMeasurementStore.hpp>
#include <state/document/BoundedDocumentReader.hpp>
#include <state/persistence/PersistentDocumentOperations.hpp>

namespace btrfsbackup::state {

class FileTargetStorageMeasurementStore final : public btrfsbackup::backup::ITargetStorageMeasurementStore {
  public:
    explicit FileTargetStorageMeasurementStore(
        std::filesystem::path state_root,
        IAtomicDocumentWriter* files = nullptr
    );

    void write(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::backup::TargetStorageMeasurement& measurement
    ) override;
    [[nodiscard]] std::optional<btrfsbackup::backup::TargetStorageMeasurement> read_matching(
        const btrfsbackup::config::Profile& profile
    ) const override;

  private:
    [[nodiscard]] std::filesystem::path document_path(const ProfileId& profile_id) const;

    std::filesystem::path state_root_;
    IAtomicDocumentWriter* files_;
    document::BoundedDocumentReader reader_;
};

} // namespace btrfsbackup::state
