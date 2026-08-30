// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileActivationMigration.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<std::string> fields_for(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> fields;
    std::string field;
    while (input >> field) {
        if (field.starts_with('#')) {
            break;
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

void validate_legacy_options(const std::vector<std::string>& fields) {
    if (fields.size() < 4) {
        return;
    }
    std::istringstream options(fields.at(3));
    std::string option;
    while (std::getline(options, option, ',')) {
        if (option == "luks" || option == "noauto" || option == "nofail" ||
            option.starts_with("x-systemd.device-timeout=")) {
            continue;
        }
        throw btrfsbackup::ValidationError(
            "legacy crypttab option is not supported by profile activation migration: " + option
        );
    }
}

} // namespace

namespace btrfsbackup::platform::linux {

btrfsbackup::config::Profile migrate_target_activation_from_crypttab(
    btrfsbackup::config::Profile profile,
    const fs::path& crypttab_path
) {
    std::ifstream input(crypttab_path);
    if (!input) {
        throw ValidationError("cannot read legacy crypttab: " + crypttab_path.string());
    }

    const std::string expected_name = profile.target.mapper_name.value();
    const std::string expected_source = "uuid=" + lower(profile.target.luks_uuid.value());
    std::vector<std::string> match;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::vector<std::string> fields = fields_for(line);
        if (fields.empty() || fields.front() != expected_name) {
            continue;
        }
        if (fields.size() < 3 || fields.size() > 4) {
            throw ValidationError(
                "invalid legacy crypttab entry for mapper " + expected_name +
                " on line " + std::to_string(line_number)
            );
        }
        if (lower(fields.at(1)) != expected_source) {
            throw ValidationError(
                "legacy crypttab entry for mapper " + expected_name + " has a different LUKS UUID"
            );
        }
        if (!match.empty()) {
            throw ValidationError("multiple legacy crypttab entries match mapper " + expected_name);
        }
        match = std::move(fields);
    }
    if (match.empty()) {
        throw ValidationError("no legacy crypttab entry matches mapper " + expected_name);
    }
    validate_legacy_options(match);

    const std::string& key_file = match.at(2);
    if (key_file == "none" || key_file == "-") {
        profile.target.activation = btrfsbackup::config::AskPasswordActivation{};
    } else {
        const fs::path path = key_file;
        if (!path.is_absolute() || key_file.find('\\') != std::string::npos) {
            throw ValidationError(
                "legacy crypttab key file must be none, -, or an absolute path without escapes"
            );
        }
        profile.target.activation = btrfsbackup::config::KeyFileActivation{
            btrfsbackup::config::KeyFilePath{path.lexically_normal()},
        };
    }
    return profile;
}

} // namespace btrfsbackup::platform::linux
