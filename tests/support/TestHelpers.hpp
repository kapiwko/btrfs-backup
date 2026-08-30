// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

#include <core/RuntimeTime.hpp>

namespace test_helpers {

inline int failures = 0;

inline btrfsbackup::RuntimeTimePoint runtime_time(const std::string& value) {
    return *btrfsbackup::parse_utc_timestamp(value);
}

inline void fail(const std::string& name, const std::string& message) {
    ++failures;
    std::cerr << "not ok - " << name << ": " << message << '\n';
}

inline void expect_eq(const std::string& name, const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        fail(name, "expected [" + expected + "], got [" + actual + "]");
    }
}

inline void expect_contains(const std::string& name, const std::string& actual, const std::string& needle) {
    if (actual.find(needle) == std::string::npos) {
        fail(name, "missing [" + needle + "] in [" + actual + "]");
    }
}

inline void expect_true(const std::string& name, bool condition, const std::string& message) {
    if (!condition) {
        fail(name, message);
    }
}

inline std::filesystem::path test_root(const std::string& suite, const std::string& name) {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("btrfsbackup-" + suite + "-tests-" + std::to_string(getpid()) + "-" + name);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

inline void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << content;
}

inline int finish(const std::string& message) {
    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - " << message << '\n';
    return 0;
}

} // namespace test_helpers
