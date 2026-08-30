// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/LinuxSystemConfigurationActivator.hpp>

#include <platform/linux/process/Process.hpp>

namespace btrfsbackup::platform::linux {

void LinuxSystemConfigurationActivator::activate() {
    (void)process::run_capture({"systemctl", "daemon-reload"});
    (void)process::run_capture({"udevadm", "control", "--reload-rules"});
}

} // namespace btrfsbackup::platform::linux
