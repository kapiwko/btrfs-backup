// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <config/profile.hpp>
#include <config/profile_artifact_renderer.hpp>

namespace btrfsbackup {

class IProfileActivation {
  public:
    virtual ~IProfileActivation() = default;
    virtual void activate() = 0;
};

class NullProfileActivation final : public IProfileActivation {
  public:
    void activate() override;
};

class FunctionProfileActivation final : public IProfileActivation {
  public:
    explicit FunctionProfileActivation(std::function<void()> activate);
    void activate() override;

  private:
    std::function<void()> activate_;
};

class ProfileInstaller {
  public:
    ProfileInstaller(ProfileArtifactRenderer& renderer, IProfileActivation& activation);

    void install_profile_transactionally(const Profile& profile, const ProfileArtifactRoots& roots);

  private:
    ProfileArtifactRenderer& renderer_;
    IProfileActivation& activation_;
};

} // namespace btrfsbackup
