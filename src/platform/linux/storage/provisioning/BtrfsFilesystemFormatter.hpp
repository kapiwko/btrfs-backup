// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::backup {
class ICommandRunner;
}

namespace btrfsbackup::platform::linux::storage::provisioning {

class IBtrfsFilesystemFormatter {
  public:
    virtual ~IBtrfsFilesystemFormatter() = default;
    virtual void format(const std::filesystem::path& device, const std::string& label) = 0;
};

class CommandBtrfsFilesystemFormatter final : public IBtrfsFilesystemFormatter {
  public:
    explicit CommandBtrfsFilesystemFormatter(backup::ICommandRunner& commands);
    void format(const std::filesystem::path& device, const std::string& label) override;

  private:
    backup::ICommandRunner& commands_;
};

} // namespace btrfsbackup::platform::linux::storage::provisioning
