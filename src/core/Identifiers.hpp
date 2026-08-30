// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <compare>
#include <string>
#include <string_view>

namespace btrfsbackup {

class ProfileId {
  public:
    explicit ProfileId(std::string value);

    [[nodiscard]] std::string_view value() const noexcept;

    auto operator<=>(const ProfileId&) const = default;

  private:
    std::string value_;
};

class RunId {
  public:
    explicit RunId(std::string value);

    [[nodiscard]] std::string_view value() const noexcept;

    auto operator<=>(const RunId&) const = default;

  private:
    std::string value_;
};

class OperationId {
  public:
    explicit OperationId(std::string value);

    [[nodiscard]] std::string_view value() const noexcept;

    auto operator<=>(const OperationId&) const = default;

  private:
    std::string value_;
};

class SourceId {
  public:
    explicit SourceId(std::string value);

    [[nodiscard]] std::string_view value() const noexcept;

    auto operator<=>(const SourceId&) const = default;

  private:
    std::string value_;
};

void validate_identifier(const std::string& value, const std::string& field_name);
void validate_profile_id(const std::string& profile_id);
void validate_run_id(const std::string& run_id);
void validate_operation_id(const std::string& operation_id);

} // namespace btrfsbackup
