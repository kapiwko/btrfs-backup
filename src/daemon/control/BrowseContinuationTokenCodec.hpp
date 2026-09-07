// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <core/Identifiers.hpp>

namespace btrfsbackup::daemon::control {

class BrowseContinuationTokenCodec final {
  public:
    [[nodiscard]] std::string directory_token(
        const BrowseSessionId& session_id,
        const std::string& relative_path,
        const std::string& last_name
    ) const;
    [[nodiscard]] std::string directory_cursor(
        const BrowseSessionId& session_id,
        const std::string& relative_path,
        const std::string& token
    ) const;
    [[nodiscard]] std::string previous_versions_token(
        const BrowseSessionId& session_id,
        const std::string& profile_id,
        const std::string& source_id,
        const std::string& relative_path,
        std::size_t offset
    ) const;
    [[nodiscard]] std::size_t previous_versions_offset(
        const BrowseSessionId& session_id,
        const std::string& profile_id,
        const std::string& source_id,
        const std::string& relative_path,
        const std::string& token
    ) const;

  private:
    static constexpr std::size_t maximum_token_size = 32768;

    [[nodiscard]] static char hex_digit(unsigned int value);
    [[nodiscard]] static std::string hex_encode(std::string_view value);
    [[nodiscard]] static int hex_value(char value);
    [[nodiscard]] static std::string directory_binding(
        const BrowseSessionId& session_id,
        const std::string& relative_path
    );
    [[nodiscard]] static std::string previous_versions_binding(
        const BrowseSessionId& session_id,
        const std::string& profile_id,
        const std::string& source_id,
        const std::string& relative_path
    );
    [[nodiscard]] static std::string encode_bound_token(
        std::string_view binding,
        std::string_view cursor
    );
    [[nodiscard]] static std::string decode_bound_token(
        std::string_view binding,
        const std::string& token
    );
};

} // namespace btrfsbackup::daemon::control
