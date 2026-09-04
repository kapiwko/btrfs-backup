// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace btrfsbackup::integration {

class RealPackageTest final {
  public:
    RealPackageTest(
        std::filesystem::path package_directory,
        std::filesystem::path source_directory
    );

    void install_and_verify() const;

  private:
    [[nodiscard]] std::filesystem::path base_package() const;

    std::filesystem::path package_directory_;
    std::filesystem::path source_directory_;
};

} // namespace btrfsbackup::integration
