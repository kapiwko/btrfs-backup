// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/linux_system_configuration_activator.hpp>

#include <platform/linux/process.hpp>

namespace btrfsbackup {

void LinuxSystemConfigurationActivator::activate() {
    (void)run_capture({"systemctl", "daemon-reload"});
    (void)run_capture({"udevadm", "control", "--reload-rules"});
}

} // namespace btrfsbackup
