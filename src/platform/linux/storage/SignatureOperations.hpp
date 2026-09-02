// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace btrfsbackup::platform::linux::storage {

struct SignatureExpectation {
    std::string type;
    std::string version;
    std::string label;
    std::string uuid;

    bool operator==(const SignatureExpectation&) const = default;
};

class ISignatureOperations {
  public:
    virtual ~ISignatureOperations() = default;
    virtual void wipe_all(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::optional<SignatureExpectation>& expected_signature = std::nullopt
    ) = 0;
};

class LibblkidSignatureOperations final : public ISignatureOperations {
  public:
    void wipe_all(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::optional<SignatureExpectation>& expected_signature = std::nullopt
    ) override;
};

} // namespace btrfsbackup::platform::linux::storage
