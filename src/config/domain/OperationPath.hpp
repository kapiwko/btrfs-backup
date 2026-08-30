// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace btrfsbackup::config {

namespace detail {

class OperationPathValue {
  public:
    [[nodiscard]] const std::filesystem::path& value() const noexcept;
    operator const std::filesystem::path&() const noexcept;
    bool operator==(const std::filesystem::path& other) const noexcept;
    bool operator==(const OperationPathValue&) const = default;

  protected:
    OperationPathValue() = default;
    OperationPathValue(std::filesystem::path value, const char* field_name);

  private:
    std::filesystem::path value_;
};

} // namespace detail

#define BTRFSBACKUP_DECLARE_OPERATION_PATH(name)      \
    class name : public detail::OperationPathValue {  \
      public:                                         \
        explicit name(std::filesystem::path value);   \
        using detail::OperationPathValue::operator==; \
        bool operator==(const name&) const = default; \
    }

BTRFSBACKUP_DECLARE_OPERATION_PATH(TargetDevicePath);
BTRFSBACKUP_DECLARE_OPERATION_PATH(TargetMountPoint);
BTRFSBACKUP_DECLARE_OPERATION_PATH(SourceSubvolumePath);
BTRFSBACKUP_DECLARE_OPERATION_PATH(LocalSnapshotRoot);
BTRFSBACKUP_DECLARE_OPERATION_PATH(KeyFilePath);

class HookProgramPath : public detail::OperationPathValue {
  public:
    HookProgramPath() = default;
    explicit HookProgramPath(std::filesystem::path value);
    using detail::OperationPathValue::operator==;
    bool operator==(const HookProgramPath&) const = default;
};

#undef BTRFSBACKUP_DECLARE_OPERATION_PATH

} // namespace btrfsbackup::config
