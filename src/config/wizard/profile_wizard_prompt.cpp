// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/wizard/profile_wizard_prompt.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <istream>
#include <ostream>
#include <string>

#include <config/errors.hpp>

namespace btrfsbackup::wizard {

std::string trim_text(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

bool parse_bool(const std::string& value) {
    std::string normalized = lower(trim_text(value));
    if (normalized == "1" || normalized == "yes" || normalized == "true" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "no" || normalized == "false" || normalized == "off") {
        return false;
    }
    throw ValidationError("enter true or false");
}

std::uint64_t parse_uint(const std::string& value) {
    std::string normalized = trim_text(value);
    if (normalized.empty() || !std::all_of(normalized.begin(), normalized.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw ValidationError("enter a non-negative integer");
    }
    try {
        return std::stoull(normalized);
    } catch (const std::exception&) {
        throw ValidationError("enter a non-negative integer");
    }
}

std::string prompt_value(std::istream& input, std::ostream& output, const std::string& label, const std::string& default_value) {
    output << label << " [" << default_value << "]: " << std::flush;
    std::string line;
    if (!std::getline(input, line)) {
        throw ValidationError("input ended while reading: " + label);
    }
    line = trim_text(line);
    return line.empty() ? default_value : line;
}

bool prompt_bool(std::istream& input, std::ostream& output, const std::string& label, bool default_value) {
    std::string default_text = default_value ? "true" : "false";
    while (true) {
        try {
            return parse_bool(prompt_value(input, output, label, default_text));
        } catch (const ValidationError& error) {
            output << error.what() << '\n';
        }
    }
}

std::uint64_t prompt_uint(
    std::istream& input,
    std::ostream& output,
    const std::string& label,
    std::uint64_t default_value
) {
    while (true) {
        try {
            return parse_uint(prompt_value(input, output, label, std::to_string(default_value)));
        } catch (const ValidationError& error) {
            output << error.what() << '\n';
        }
    }
}

} // namespace btrfsbackup::wizard
