// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd_unit.hpp>

#include <cctype>

namespace btrfsbackup::platform::linux {

namespace {

bool systemd_unit_plain_char(unsigned char value) {
    return std::isalnum(value) || value == ':' || value == '_' || value == '.';
}

std::string systemd_hex_escape(unsigned char value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "\\x";
    result.push_back(digits[(value >> 4U) & 0x0fU]);
    result.push_back(digits[value & 0x0fU]);
    return result;
}

} // namespace

std::string systemd_mount_unit_name(const std::filesystem::path& mount_point) {
    std::string path = mount_point.lexically_normal().string();
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    if (path == "/") {
        return "-.mount";
    }
    if (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    std::string escaped;
    bool previous_slash = false;
    for (const char character : path) {
        const auto value = static_cast<unsigned char>(character);
        if (value == '/') {
            if (!escaped.empty() && !previous_slash) {
                escaped.push_back('-');
            }
            previous_slash = true;
            continue;
        }
        previous_slash = false;
        escaped += systemd_unit_plain_char(value) ? std::string(1, character) : systemd_hex_escape(value);
    }
    return (escaped.empty() ? "-" : escaped) + ".mount";
}

std::string systemd_cryptsetup_unit_name(const std::string& mapper_name) {
    std::string escaped;
    for (const char character : mapper_name) {
        const auto value = static_cast<unsigned char>(character);
        escaped += systemd_unit_plain_char(value) ? std::string(1, character) : systemd_hex_escape(value);
    }
    return "systemd-cryptsetup@" + escaped + ".service";
}

} // namespace btrfsbackup::platform::linux
