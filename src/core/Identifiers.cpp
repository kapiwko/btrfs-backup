// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <core/Identifiers.hpp>

#include <regex>
#include <string>
#include <utility>

#include <core/Errors.hpp>

namespace {

const std::regex identifier_re{"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"};
const std::regex run_id_re{"^[A-Za-z0-9][A-Za-z0-9._:-]*$"};

} // namespace

namespace btrfsbackup {

ProfileId::ProfileId(std::string value) : value_(std::move(value)) {
    validate_profile_id(value_);
}

std::string_view ProfileId::value() const noexcept {
    return value_;
}

RunId::RunId(std::string value) : value_(std::move(value)) {
    validate_run_id(value_);
}

OperationId::OperationId(std::string value) : value_(std::move(value)) {
    validate_operation_id(value_);
}

std::string_view OperationId::value() const noexcept {
    return value_;
}

BrowseSessionId::BrowseSessionId(std::string value) : value_(std::move(value)) {
    validate_browse_session_id(value_);
}

std::string_view BrowseSessionId::value() const noexcept {
    return value_;
}

std::string_view RunId::value() const noexcept {
    return value_;
}

SourceId::SourceId(std::string value) : value_(std::move(value)) {
    validate_identifier(value_, "sourceId");
}

std::string_view SourceId::value() const noexcept {
    return value_;
}

void validate_identifier(const std::string& value, const std::string& field_name) {
    if (!std::regex_match(value, identifier_re)) {
        throw ValidationError(field_name + " contains unsupported characters");
    }
}

void validate_profile_id(const std::string& profile_id) {
    try {
        validate_identifier(profile_id, "profileId");
    } catch (const ValidationError&) {
        throw ValidationError("invalid profile id: " + profile_id);
    }
}

void validate_run_id(const std::string& run_id) {
    if (!std::regex_match(run_id, run_id_re)) {
        throw ValidationError("invalid run id: " + run_id);
    }
}

void validate_operation_id(const std::string& operation_id) {
    validate_identifier(operation_id, "operationId");
}

void validate_browse_session_id(const std::string& browse_session_id) {
    validate_identifier(browse_session_id, "browseSessionId");
}

} // namespace btrfsbackup
