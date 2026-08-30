// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd/SystemdUnit.hpp>

#include <array>
#include <cctype>

namespace btrfsbackup::platform::linux::systemd {

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

std::string target_activation_unit_name(std::string_view profile_id) {
    std::string escaped;
    for (const char character : profile_id) {
        const auto value = static_cast<unsigned char>(character);
        escaped += systemd_unit_plain_char(value) ? std::string(1, character) : systemd_hex_escape(value);
    }
    return "btrfs-backup-target@" + escaped + ".service";
}

std::optional<std::filesystem::path> locate_systemd_unit_file(
    std::string_view unit_name,
    std::span<const std::filesystem::path> unit_roots
) {
    const std::filesystem::path name{unit_name};
    if (name.empty() || name.is_absolute() || name.filename() != name) {
        return std::nullopt;
    }
    for (const std::filesystem::path& root : unit_roots) {
        const std::filesystem::path candidate = root / name;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> locate_systemd_unit_file(
    std::string_view unit_name
) {
    static const std::array<std::filesystem::path, 5> unit_roots = {
        "/etc/systemd/system",
        "/run/systemd/system",
        "/usr/local/lib/systemd/system",
        "/usr/lib/systemd/system",
        "/lib/systemd/system",
    };
    return locate_systemd_unit_file(unit_name, unit_roots);
}

} // namespace btrfsbackup::platform::linux::systemd
