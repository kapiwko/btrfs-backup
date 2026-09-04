// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::integration {

struct CommandResult {
    int status{};
    std::string output;
    std::string error_output;
};

class RealBtrfsTestEnvironment final {
  public:
    explicit RealBtrfsTestEnvironment(std::filesystem::path backupctl);
    ~RealBtrfsTestEnvironment() noexcept;

    RealBtrfsTestEnvironment(const RealBtrfsTestEnvironment&) = delete;
    RealBtrfsTestEnvironment& operator=(const RealBtrfsTestEnvironment&) = delete;
    RealBtrfsTestEnvironment(RealBtrfsTestEnvironment&&) = delete;
    RealBtrfsTestEnvironment& operator=(RealBtrfsTestEnvironment&&) = delete;

    void prepare();
    [[nodiscard]] CommandResult execute_backup(
        std::string_view timestamp,
        std::string_view run_id
    ) const;
    void write_source_file(const std::filesystem::path& relative_path, std::string_view content) const;
    void create_interrupted_receive_artifact() const;

    [[nodiscard]] std::size_t local_snapshot_count() const;
    [[nodiscard]] std::size_t remote_snapshot_count() const;
    [[nodiscard]] bool incoming_is_empty() const;
    [[nodiscard]] std::string artifact_report() const;
    void require_latest_snapshots_match() const;

    void close();

  private:
    [[nodiscard]] CommandResult command(
        std::vector<std::string> arguments,
        std::chrono::seconds timeout = std::chrono::seconds(30),
        std::string_view standard_input = {}
    ) const;
    void require_command(std::vector<std::string> arguments, std::string_view operation) const;
    [[nodiscard]] std::filesystem::path latest_snapshot(const std::filesystem::path& root) const;
    void write_configuration(const std::string& target_uuid) const;
    void verify_owned_loop(const std::string& device, const std::filesystem::path& image) const;
    [[nodiscard]] std::vector<std::string> release_resources() noexcept;

    std::filesystem::path backupctl_;
    std::filesystem::path root_;
    std::filesystem::path source_image_;
    std::filesystem::path target_image_;
    std::filesystem::path source_mount_;
    std::filesystem::path target_mount_root_;
    std::filesystem::path target_mount_;
    std::filesystem::path config_root_;
    std::filesystem::path state_root_;
    std::filesystem::path status_root_;
    std::filesystem::path history_root_;
    std::string source_loop_;
    std::string target_loop_;
    std::string mapper_name_;
    std::filesystem::path mapper_path_;
    bool source_mounted_{false};
    bool target_mounted_{false};
    bool mapper_open_{false};
    bool closed_{false};
};

} // namespace btrfsbackup::integration
