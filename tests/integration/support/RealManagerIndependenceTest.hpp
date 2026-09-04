// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace btrfsbackup::integration {

class RealManagerIndependenceTest final {
  public:
    RealManagerIndependenceTest(
        std::filesystem::path runtime,
        std::filesystem::path profile,
        std::filesystem::path test_root,
        std::filesystem::path source_mount,
        std::filesystem::path target_mount
    );
    ~RealManagerIndependenceTest() noexcept;

    RealManagerIndependenceTest(const RealManagerIndependenceTest&) = delete;
    RealManagerIndependenceTest& operator=(const RealManagerIndependenceTest&) = delete;

    void run();
    void close();

  private:
    void start_runner();
    void wait_for_hook() const;
    void release_hook();
    void wait_for_runner();
    void verify_snapshots() const;
    [[nodiscard]] std::vector<std::string> release_resources() noexcept;

    std::filesystem::path runtime_;
    std::filesystem::path profile_;
    std::filesystem::path source_mount_;
    std::filesystem::path target_mount_;
    std::filesystem::path hook_{"/etc/btrfs-backup/hooks.d/manager-independence-test"};
    std::filesystem::path marker_;
    std::filesystem::path fifo_;
    std::filesystem::path log_;
    std::string original_profile_;
    pid_t runner_pid_{-1};
    bool profile_modified_{false};
    bool manager_started_{false};
    bool closed_{false};
};

} // namespace btrfsbackup::integration
