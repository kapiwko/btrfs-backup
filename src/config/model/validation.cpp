// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/model/validation.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>

#include <core/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::config {

std::uint64_t parse_uint(const std::string& value, const std::string& name, std::uint64_t maximum) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw ValidationError(name + " must be a non-negative base-10 integer");
    }
    std::size_t consumed = 0;
    std::uint64_t result = 0;
    try {
        result = std::stoull(value, &consumed, 10);
    } catch (const std::exception&) {
        throw ValidationError(name + " is outside the supported range");
    }
    if (consumed != value.size() || result > maximum) {
        throw ValidationError(name + " is outside the supported range");
    }
    return result;
}

std::uint64_t parse_positive_uint(const std::string& value, const std::string& name, std::uint64_t maximum) {
    const std::uint64_t result = parse_uint(value, name, maximum);
    if (result == 0) {
        throw ValidationError(name + " must be greater than zero");
    }
    return result;
}

fs::path normalized_path(const fs::path& path) {
    return path.lexically_normal();
}

fs::path normalized_absolute_path(const std::string& value, const std::string& name) {
    if (value.empty() || value.front() != '/') {
        throw ValidationError(name + " must be an absolute path");
    }
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw ValidationError(name + " contains a newline");
    }
    fs::path result = normalized_path(value);
    if (!result.is_absolute()) {
        throw ValidationError(name + " is invalid");
    }
    return result;
}

fs::path normalized_relative_path(const std::string& value, const std::string& name) {
    if (value.empty()) {
        throw ValidationError(name + " must be a non-empty relative path");
    }
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw ValidationError(name + " contains a newline");
    }
    fs::path path(value);
    if (path.is_absolute()) {
        throw ValidationError(name + " must be a safe relative path");
    }
    for (const auto& part : path) {
        std::string item = part.string();
        if (item.empty() || item == "." || item == "..") {
            throw ValidationError(name + " must be a safe relative path");
        }
    }
    return normalized_path(path);
}

bool path_is_within(const fs::path& candidate, const fs::path& base) {
    fs::path normalized_candidate = normalized_path(candidate);
    fs::path normalized_base = normalized_path(base);
    auto candidate_it = normalized_candidate.begin();
    auto base_it = normalized_base.begin();
    for (; base_it != normalized_base.end(); ++base_it, ++candidate_it) {
        if (candidate_it == normalized_candidate.end() || *candidate_it != *base_it) {
            return false;
        }
    }
    return true;
}

} // namespace btrfsbackup::config
