// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/model/StoragePolicy.hpp>

#include <core/Errors.hpp>

namespace btrfsbackup::config {

namespace {

std::size_t validated_retention_count(std::uint64_t value) {
    if (value > RetentionCount::maximum) {
        throw ValidationError("retention count is outside the supported range");
    }
    return static_cast<std::size_t>(value);
}

} // namespace

RetentionCount::RetentionCount(std::uint64_t value) : value_(validated_retention_count(value)) {
}

std::size_t RetentionCount::value() const noexcept {
    return value_;
}

ByteThreshold::ByteThreshold(std::uint64_t value) : value_(value) {
    if (value > maximum) {
        throw ValidationError("byte threshold is outside the supported range");
    }
}

std::uint64_t ByteThreshold::value() const noexcept {
    return value_;
}

} // namespace btrfsbackup::config
