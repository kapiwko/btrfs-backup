// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/system_configuration_activator.hpp>

#include <platform/linux/process.hpp>

namespace btrfsbackup::platform::linux {

void LinuxSystemConfigurationActivator::activate() {
    (void)run_capture({"systemctl", "daemon-reload"});
    (void)run_capture({"udevadm", "control", "--reload-rules"});
}

} // namespace btrfsbackup::platform::linux
