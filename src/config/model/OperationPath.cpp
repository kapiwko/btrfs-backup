// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/model/OperationPath.hpp>

#include <utility>

#include <config/model/Validation.hpp>
#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::config {
detail::OperationPathValue::OperationPathValue(fs::path value, const char* field_name)
    : value_(normalized_absolute_path(value.string(), field_name)) {
}

const fs::path& detail::OperationPathValue::value() const noexcept {
    return value_;
}

detail::OperationPathValue::operator const fs::path&() const noexcept {
    return value_;
}

bool detail::OperationPathValue::operator==(const fs::path& other) const noexcept {
    return value_ == other;
}

TargetDevicePath::TargetDevicePath(fs::path value)
    : detail::OperationPathValue(std::move(value), "target.device") {
    if (!path_is_within(this->value(), "/dev")) {
        throw ValidationError("target.device must point inside /dev");
    }
}

TargetMountPoint::TargetMountPoint(fs::path value)
    : detail::OperationPathValue(std::move(value), "target.mountPoint") {
}

SourceSubvolumePath::SourceSubvolumePath(fs::path value)
    : detail::OperationPathValue(std::move(value), "source.subvolume") {
}

LocalSnapshotRoot::LocalSnapshotRoot(fs::path value)
    : detail::OperationPathValue(std::move(value), "source.localSnapshotDir") {
}

HookProgramPath::HookProgramPath(fs::path value)
    : detail::OperationPathValue(std::move(value), "hook.program") {
}

} // namespace btrfsbackup::config
