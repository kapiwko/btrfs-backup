// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/profile_artifact_renderer.hpp>

#include <sys/random.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>

#include <core/errors.hpp>
#include <platform/linux/file_io.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

std::string generate_configuration_generation() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ValidationError("cannot generate configuration generation");
        }
        offset += static_cast<std::size_t>(count);
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

void write_profile_artifacts(const RenderedProfileArtifacts& rendered) {
    for (const ProfileArtifact& artifact : rendered.artifacts) {
        atomic_write(
            artifact.destination,
            artifact.content,
            static_cast<mode_t>(artifact.permissions)
        );
    }
}

} // namespace btrfsbackup
