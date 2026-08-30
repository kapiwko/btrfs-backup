// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cerrno>
#include <exception>
#include <functional>
#include <utility>

namespace btrfsbackup::daemon {

template <typename Callback, typename ErrorHandler>
int invoke_dbus_callback(Callback&& callback, ErrorHandler&& handle_error) noexcept {
    try {
        return std::invoke(std::forward<Callback>(callback));
    } catch (const std::exception& exception) {
        try {
            return std::invoke(std::forward<ErrorHandler>(handle_error), &exception);
        } catch (...) {
            return -EIO;
        }
    } catch (...) {
        try {
            return std::invoke(std::forward<ErrorHandler>(handle_error), nullptr);
        } catch (...) {
            return -EIO;
        }
    }
}

} // namespace btrfsbackup::daemon
