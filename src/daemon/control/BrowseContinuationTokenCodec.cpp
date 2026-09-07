// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseContinuationTokenCodec.hpp>

#include <charconv>
#include <filesystem>

#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

char BrowseContinuationTokenCodec::hex_digit(unsigned int value) {
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

std::string BrowseContinuationTokenCodec::hex_encode(std::string_view value) {
    std::string result;
    result.reserve(value.size() * 2);
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(hex_digit(byte >> 4));
        result.push_back(hex_digit(byte & 0x0f));
    }
    return result;
}

int BrowseContinuationTokenCodec::hex_value(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

std::string BrowseContinuationTokenCodec::directory_binding(
    const BrowseSessionId& session_id,
    const std::string& relative_path
) {
    return std::string(session_id.value()) + '\0' +
        std::filesystem::path(relative_path).lexically_normal().generic_string() + '\0';
}

std::string BrowseContinuationTokenCodec::previous_versions_binding(
    const BrowseSessionId& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& relative_path
) {
    std::string normalized_path = std::filesystem::path(relative_path).lexically_normal().generic_string();
    if (normalized_path.empty())
        normalized_path = ".";
    return std::string(session_id.value()) + '\0' + profile_id + '\0' + source_id + '\0' +
        normalized_path + '\0';
}

std::string BrowseContinuationTokenCodec::encode_bound_token(
    std::string_view binding,
    std::string_view cursor
) {
    std::string token = "v1:" + hex_encode(std::string(binding) + std::string(cursor));
    if (token.size() > maximum_token_size)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse continuation token is too large");
    return token;
}

std::string BrowseContinuationTokenCodec::decode_bound_token(
    std::string_view binding,
    const std::string& token
) {
    if (token.empty())
        return {};
    if (!token.starts_with("v1:") || token.size() > maximum_token_size || (token.size() - 3) % 2 != 0)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid browse continuation token");
    std::string decoded;
    decoded.reserve((token.size() - 3) / 2);
    for (std::size_t index = 3; index < token.size(); index += 2) {
        const int high = hex_value(token[index]);
        const int low = hex_value(token[index + 1]);
        if (high < 0 || low < 0)
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid browse continuation token");
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    if (!decoded.starts_with(binding))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse continuation token does not match the session and query");
    return decoded.substr(binding.size());
}

std::string BrowseContinuationTokenCodec::directory_token(
    const BrowseSessionId& session_id,
    const std::string& relative_path,
    const std::string& last_name
) const {
    return encode_bound_token(directory_binding(session_id, relative_path), last_name);
}

std::string BrowseContinuationTokenCodec::directory_cursor(
    const BrowseSessionId& session_id,
    const std::string& relative_path,
    const std::string& token
) const {
    if (token.empty())
        return {};
    const std::string name = decode_bound_token(directory_binding(session_id, relative_path), token);
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
        name.find('\0') != std::string::npos)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid browse continuation token");
    return name;
}

std::string BrowseContinuationTokenCodec::previous_versions_token(
    const BrowseSessionId& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& relative_path,
    std::size_t offset
) const {
    if (offset == 0)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid previous-versions continuation offset");
    return encode_bound_token(
        previous_versions_binding(session_id, profile_id, source_id, relative_path),
        std::to_string(offset)
    );
}

std::size_t BrowseContinuationTokenCodec::previous_versions_offset(
    const BrowseSessionId& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& relative_path,
    const std::string& token
) const {
    if (token.empty())
        return 0;
    const std::string value = decode_bound_token(
        previous_versions_binding(session_id, profile_id, source_id, relative_path),
        token
    );
    std::size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid previous-versions continuation token");
    return result;
}

} // namespace btrfsbackup::daemon::control
