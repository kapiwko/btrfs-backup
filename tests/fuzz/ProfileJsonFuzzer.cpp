// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>

#include <config/json/ProfileDocument.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using btrfsbackup::config::json::Json;

    std::string bytes;
    if (size != 0) {
        bytes.assign(reinterpret_cast<const char*>(data), size);
    }

    std::optional<Json> input;
    try {
        input.emplace(Json::parse(bytes.begin(), bytes.end()));
    } catch (const std::exception&) {
        return 0;
    }

    std::optional<btrfsbackup::config::Profile> profile;
    try {
        profile.emplace(btrfsbackup::config::json::profile_from_json(*input));
    } catch (const std::exception&) {
        // Rejected profile fields are expected fuzzing outcomes.
        return 0;
    }

    const Json encoded = btrfsbackup::config::json::profile_to_json(*profile);
    (void)btrfsbackup::config::json::profile_from_json(encoded);
    return 0;
}
