// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <config/domain/Profile.hpp>
#include <config/ProfileArtifactRenderer.hpp>
#include <config/ports/ConfigurationActivator.hpp>

namespace btrfsbackup::platform::linux::config {

struct ExpectedProfileIdentity {
    bool exists;
    std::string generation;
    std::string fingerprint;
};

// Publishes rendered artifacts using Linux locking and activation semantics.

class ProfileInstaller {
  public:
    ProfileInstaller(btrfsbackup::config::ProfileArtifactRenderer& renderer, btrfsbackup::config::IConfigurationActivator& activator);

    void install_profile_transactionally(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::config::ProfileArtifactRoots& roots,
        const ExpectedProfileIdentity* expected = nullptr
    );
    void delete_profile_transactionally(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::config::ProfileArtifactRoots& roots,
        const ExpectedProfileIdentity* expected = nullptr
    );

  private:
    btrfsbackup::config::ProfileArtifactRenderer& renderer_;
    btrfsbackup::config::IConfigurationActivator& activator_;
};

} // namespace btrfsbackup::platform::linux::config
