// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <config/model/Json.hpp>
#include <daemon/ManagerAuditLog.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_audit_log_appends_durable_structured_records() {
    const fs::path root = test_helpers::test_root("manager-audit-log", "append");
    const fs::path path = root / "audit" / "manager.jsonl";
    {
        btrfsbackup::daemon::FileManagerAuditLog audit(path);
        test_helpers::expect_true(
            "accepted audit write",
            !audit.write({1000, "start-backup", "default", "accepted", "none"}).has_value(),
            "accepted audit record failed"
        );
        test_helpers::expect_true(
            "denied audit write",
            !audit.write({1000, "eject-target", "default", "denied", "io.github.btrfsbackup.Error.NotAuthorized"}).has_value(),
            "denied audit record failed"
        );
    }

    std::ifstream input(path);
    std::string first_line;
    std::string second_line;
    std::getline(input, first_line);
    std::getline(input, second_line);
    const btrfsbackup::config::Json first = btrfsbackup::config::Json::parse(first_line);
    const btrfsbackup::config::Json second = btrfsbackup::config::Json::parse(second_line);
    test_helpers::expect_true("audit schema", first.at("schemaVersion") == 1, "wrong audit schema");
    test_helpers::expect_true("audit uid", first.at("callerUid") == 1000, "caller UID was lost");
    test_helpers::expect_true("audit action", first.at("action") == "start-backup", "action was lost");
    test_helpers::expect_true("audit profile", first.at("profileId") == "default", "profile was lost");
    test_helpers::expect_true("audit accepted", first.at("result") == "accepted", "accepted result was lost");
    test_helpers::expect_true(
        "audit denial",
        second.at("result") == "denied" &&
            second.at("errorCode") == "io.github.btrfsbackup.Error.NotAuthorized",
        "denial code was lost"
    );
    test_helpers::expect_true("audit timestamp", !first.at("timestamp").get<std::string>().empty(), "timestamp was omitted");

    struct stat status{};
    test_helpers::expect_true("audit stat", stat(path.c_str(), &status) == 0, "cannot inspect audit log");
    test_helpers::expect_true("audit permissions", (status.st_mode & 0777) == 0600, "audit log is not root-only");
    fs::remove_all(root);
}

void test_audit_log_rejects_symlink() {
    const fs::path root = test_helpers::test_root("manager-audit-log", "symlink");
    fs::create_directories(root);
    const fs::path target = root / "target";
    std::ofstream(target) << "existing\n";
    const fs::path link = root / "audit.jsonl";
    fs::create_symlink(target, link);

    try {
        btrfsbackup::daemon::FileManagerAuditLog audit(link);
        test_helpers::fail("audit symlink", "symlink was accepted");
    } catch (const std::runtime_error&) {
    }
    fs::remove_all(root);
}

void test_audit_log_preserves_existing_parent_permissions() {
    const fs::path root = test_helpers::test_root("manager-audit-log", "parent-permissions");
    const fs::path parent = root / "existing";
    fs::create_directories(parent);
    fs::permissions(parent, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec);

    {
        btrfsbackup::daemon::FileManagerAuditLog audit(parent / "manager.jsonl");
    }

    const fs::perms permissions = fs::status(parent).permissions();
    test_helpers::expect_true(
        "audit parent permissions",
        (permissions & fs::perms::group_read) != fs::perms::none &&
            (permissions & fs::perms::group_exec) != fs::perms::none,
        "audit log changed existing parent permissions"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_audit_log_appends_durable_structured_records();
    test_audit_log_rejects_symlink();
    test_audit_log_preserves_existing_parent_permissions();
    return test_helpers::finish("manager audit log tests");
}
