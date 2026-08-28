// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <backup/ports/trusted_executable.hpp>

namespace test_support {

class FakeTrustedExecutable final : public btrfsbackup::backup::ITrustedExecutable {
  public:
    explicit FakeTrustedExecutable(std::string path) : path_(std::move(path)) {
    }

    [[nodiscard]] std::string execution_path() const override {
        return path_;
    }

    [[nodiscard]] std::vector<int> inherited_fds() const override {
        return {};
    }

  private:
    std::string path_;
};

class FakeTrustedExecutableResolver final : public btrfsbackup::backup::ITrustedExecutableResolver {
  public:
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ITrustedExecutable> resolve(
        const std::filesystem::path& program
    ) const override {
        return std::make_unique<FakeTrustedExecutable>(program.string());
    }
};

} // namespace test_support
