// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/LinuxSystemConfigurationActivator.hpp>

#include <platform/linux/Process.hpp>

namespace btrfsbackup::platform::linux {

void LinuxSystemConfigurationActivator::activate() {
    (void)run_capture({"systemctl", "daemon-reload"});
    (void)run_capture({"udevadm", "control", "--reload-rules"});
}

} // namespace btrfsbackup::platform::linux
