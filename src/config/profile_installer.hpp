// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/model/profile.hpp>
#include <config/profile_artifact_renderer.hpp>
#include <config/ports/configuration_activator.hpp>

namespace btrfsbackup {

class ProfileInstaller {
  public:
    ProfileInstaller(ProfileArtifactRenderer& renderer, IConfigurationActivator& activator);

    void install_profile_transactionally(const Profile& profile, const ProfileArtifactRoots& roots);

  private:
    ProfileArtifactRenderer& renderer_;
    IConfigurationActivator& activator_;
};

} // namespace btrfsbackup
