// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <config/model/profile.hpp>

namespace btrfsbackup::backup {

enum class TargetMountMode {
    RequireMounted,
    MountIfNeeded,
};

enum class TargetCleanupStage {
    MountUnit,
    CryptsetupUnit,
};

struct TargetCleanupError {
    TargetCleanupStage stage;
    std::string unit;
    int exit_code = -1;
    std::string message;
};

class IMountedTargetSession {
  public:
    virtual ~IMountedTargetSession() = default;
    [[nodiscard]] virtual bool mounted_by_this_session() const noexcept = 0;
    [[nodiscard]] virtual std::optional<TargetCleanupError> close() noexcept = 0;
};

class ITargetManager {
  public:
    virtual ~ITargetManager() = default;
    [[nodiscard]] virtual std::unique_ptr<IMountedTargetSession> prepare(
        const btrfsbackup::config::Profile& profile,
        TargetMountMode mode
    ) = 0;
};

} // namespace btrfsbackup::backup
