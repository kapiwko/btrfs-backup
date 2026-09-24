// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <platform/linux/config/ProfileConfigurationTransaction.hpp>

namespace btrfsbackup::daemon::control {

struct ProfileStateRoots {
    std::filesystem::path state_root;
    std::filesystem::path status_root;
    std::filesystem::path history_root;
};

class ProfileStateQuarantine final {
  public:
    ProfileStateQuarantine(ProfileStateRoots roots, std::string profile_id, std::string fingerprint);

    void quarantine();
    [[nodiscard]] platform::linux::config::RollbackResult rollback() noexcept;
    void finish() noexcept;

  private:
    struct Move {
        std::filesystem::path source;
        std::filesystem::path destination;
        bool moved = false;
    };

    static void create_private_directory(const std::filesystem::path& path);
    static void create_transaction_directories(
        const std::filesystem::path& retired_root,
        const std::filesystem::path& profile_root,
        const std::filesystem::path& transaction_root
    );
    static bool movable_directory_if_present(const std::filesystem::path& path);
    static void move_directory(Move& move);
    static void record_rollback_error(
        platform::linux::config::RollbackResult& result,
        const std::string& operation,
        const std::filesystem::path& path,
        const std::string& message
    ) noexcept;
    void remove_empty_transaction_directories() noexcept;

    ProfileStateRoots roots_;
    std::filesystem::path state_retired_root_;
    std::filesystem::path state_profile_root_;
    std::filesystem::path state_transaction_root_;
    std::filesystem::path history_retired_root_;
    std::filesystem::path history_profile_root_;
    std::filesystem::path history_transaction_root_;
    std::filesystem::path status_retired_root_;
    std::filesystem::path status_profile_root_;
    std::filesystem::path status_transaction_root_;
    std::vector<Move> moves_;
};

} // namespace btrfsbackup::daemon::control
