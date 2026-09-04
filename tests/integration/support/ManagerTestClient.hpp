// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace btrfsbackup::integration {

class ManagerTestClient final {
  public:
    ManagerTestClient();
    ~ManagerTestClient() noexcept;

    ManagerTestClient(const ManagerTestClient&) = delete;
    ManagerTestClient& operator=(const ManagerTestClient&) = delete;

    [[nodiscard]] std::string call(std::string_view method) const;
    [[nodiscard]] std::string call(std::string_view method, std::string_view argument) const;
    [[nodiscard]] std::string call_with_fd(
        std::string_view method,
        std::string_view argument,
        int descriptor
    ) const;

  private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace btrfsbackup::integration
