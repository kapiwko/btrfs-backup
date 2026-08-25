// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include <config/profile.hpp>

namespace btrfsbackup {

class CancellationToken;

class TerminationSignalMonitor {
public:
    explicit TerminationSignalMonitor(CancellationToken& cancellation);
    TerminationSignalMonitor(const TerminationSignalMonitor&) = delete;
    TerminationSignalMonitor& operator=(const TerminationSignalMonitor&) = delete;
    ~TerminationSignalMonitor();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct BackupToolServices {
    std::function<int(const std::vector<std::string>&, std::ostream&)> runner;
    std::function<int(const std::vector<std::string>&, std::ostream&)> target;
    std::function<Profile(const std::string&)> load_profile;
    std::function<bool()> is_service_invocation;
};

int backup_tool(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    BackupToolServices* services = nullptr,
    CancellationToken* cancellation = nullptr
);

int backup_tool_main(int argc, char** argv);

} // namespace btrfsbackup
