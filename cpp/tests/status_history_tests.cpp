#include <filesystem>
#include <sstream>
#include <string>

#include <btrfsbackup/command/profile_list_command.hpp>
#include <btrfsbackup/command/status_history_command.hpp>
#include <btrfsbackup/command/status_show_command.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("status-history", name);
}

void test_history_without_directory_returns_empty_array() {
    fs::path root = test_root("history-empty");
    std::ostringstream output;

    btrfsbackup::command::status_history(root / "history", {}, output);

    test_helpers::expect_eq("history without directory", output.str(), "[]\n");
    fs::remove_all(root);
}

void test_status_falls_back_to_last_json() {
    fs::path root = test_root("status-fallback");
    test_helpers::write_file(root / "history" / "default" / "last.json", "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    std::ostringstream output;

    btrfsbackup::command::status_show(root / "status", root / "history", {}, output);

    test_helpers::expect_eq("status fallback", output.str(), "{\"profileId\":\"default\",\"state\":\"ok\"}\n");
    fs::remove_all(root);
}

void test_list_profiles_from_json_files() {
    fs::path root = test_root("profiles");
    test_helpers::write_file(root / "profiles" / "beta" / "profile.json", "{}\n");
    test_helpers::write_file(root / "profiles" / "default" / "profile.json", "{}\n");
    std::ostringstream output;

    btrfsbackup::command::profile_list(root / "profiles.d", root / "profiles", output);

    test_helpers::expect_eq("list profiles", output.str(), "beta\ndefault\n");
    fs::remove_all(root);
}

void test_status_human_format() {
    fs::path root = test_root("human");
    test_helpers::write_file(
        root / "status" / "default" / "current.json",
        "{"
        "\"profileId\":\"default\","
        "\"profileName\":\"Default backup\","
        "\"state\":\"running\","
        "\"phase\":\"send\","
        "\"message\":\"copying\","
        "\"currentSourceName\":\"Home\","
        "\"updatedAt\":\"2026-08-23T12:00:00Z\""
        "}\n"
    );
    std::ostringstream output;

    btrfsbackup::command::status_show(root / "status", root / "history", {"--human"}, output);

    test_helpers::expect_contains("human status", output.str(), "Default backup: running\n");
    test_helpers::expect_contains("human phase", output.str(), "  phase: send\n");
    test_helpers::expect_contains("human message", output.str(), "  copying\n");
    test_helpers::expect_contains("human source", output.str(), "  source: Home\n");
    fs::remove_all(root);
}

void test_list_profiles_rejects_invalid_name() {
    fs::path root = test_root("bad-profile");
    test_helpers::write_file(root / "profiles" / "-bad" / "profile.json", "{}\n");

    test_helpers::expect_validation_error(
        "invalid profile",
        [&] {
            std::ostringstream output;
            btrfsbackup::command::profile_list(root / "profiles.d", root / "profiles", output);
        },
        "invalid profile id"
    );
    fs::remove_all(root);
}

void test_history_limit() {
    fs::path root = test_root("history-limit");
    test_helpers::write_file(root / "history" / "default" / "2026-08-22T000000Z.json", "{\"id\":1}");
    test_helpers::write_file(root / "history" / "default" / "2026-08-23T000000Z.json", "{\"id\":2}");
    test_helpers::write_file(root / "history" / "default" / "last.json", "{\"id\":3}");
    std::ostringstream output;

    btrfsbackup::command::status_history(root / "history", {"--limit", "1"}, output);

    test_helpers::expect_eq("history limit", output.str(), "[\n{\"id\":2}\n]\n");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_history_without_directory_returns_empty_array();
    test_status_falls_back_to_last_json();
    test_list_profiles_from_json_files();
    test_status_human_format();
    test_list_profiles_rejects_invalid_name();
    test_history_limit();

    return test_helpers::finish("status/history tests");
}
