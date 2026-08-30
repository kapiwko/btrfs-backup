// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/model/TargetIdentity.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::config {

namespace {

const std::regex uuid_pattern{
    "^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"
};

std::string canonical_uuid(std::string value, const std::string& name, bool allow_empty = false) {
    if (allow_empty && value.empty()) {
        return value;
    }
    if (!std::regex_match(value, uuid_pattern)) {
        throw ValidationError(name + " is not a canonical UUID");
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

LuksUuid::LuksUuid(std::string value) : value_(canonical_uuid(std::move(value), "LUKS UUID")) {
}

const std::string& LuksUuid::value() const noexcept {
    return value_;
}

BtrfsUuid::BtrfsUuid(std::string value) : value_(canonical_uuid(std::move(value), "Btrfs UUID")) {
}

const std::string& BtrfsUuid::value() const noexcept {
    return value_;
}

PartitionUuid::PartitionUuid(std::string value)
    : value_(canonical_uuid(std::move(value), "partition UUID", true)) {
}

const std::string& PartitionUuid::value() const noexcept {
    return value_;
}

bool PartitionUuid::empty() const noexcept {
    return value_.empty();
}

MapperName::MapperName(std::string value) : value_(std::move(value)) {
    validate_identifier(value_, "mapper name");
}

const std::string& MapperName::value() const noexcept {
    return value_;
}

} // namespace btrfsbackup::config
