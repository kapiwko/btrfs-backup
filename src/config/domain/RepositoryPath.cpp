// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/domain/RepositoryPath.hpp>

#include <utility>

#include <config/domain/Validation.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::config {

namespace {

std::filesystem::path safe_relative_path(std::string value) {
    const std::filesystem::path normalized = normalized_relative_path(value, "repository relative path");
    if (normalized.string() != value) {
        throw ValidationError("repository relative path must be canonical and contain no empty segments");
    }
    return normalized;
}

} // namespace

RemoteSnapshotRoot::RemoteSnapshotRoot(std::string value)
    : value_(normalized_absolute_path(value, "remote snapshot root")) {
}

const std::filesystem::path& RemoteSnapshotRoot::value() const noexcept {
    return value_;
}

IncomingRoot::IncomingRoot(std::string value)
    : value_(normalized_absolute_path(value, "incoming root")) {
}

const std::filesystem::path& IncomingRoot::value() const noexcept {
    return value_;
}

SafeRelativePath::SafeRelativePath(std::string value) : value_(safe_relative_path(std::move(value))) {
}

const std::filesystem::path& SafeRelativePath::value() const noexcept {
    return value_;
}

} // namespace btrfsbackup::config
