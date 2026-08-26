// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace btrfsbackup {

class IConfigurationActivator {
  public:
    virtual ~IConfigurationActivator() = default;
    virtual void activate() = 0;
};

class NullConfigurationActivator final : public IConfigurationActivator {
  public:
    void activate() override {
    }
};

} // namespace btrfsbackup
