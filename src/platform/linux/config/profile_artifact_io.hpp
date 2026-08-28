// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <config/profile_artifact_renderer.hpp>

namespace btrfsbackup::platform::linux {

[[nodiscard]] std::string generate_configuration_generation();
void write_profile_artifacts(const btrfsbackup::config::RenderedProfileArtifacts& rendered);

} // namespace btrfsbackup::platform::linux
