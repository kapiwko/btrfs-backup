#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/profile_wizard_paths.hpp>
#include <btrfsbackup/profile_wizard_prompt.hpp>
#include <btrfsbackup/profile_wizard_sources.hpp>

#include "test_helpers.hpp"

namespace {

void test_prompt_parsing() {
    test_helpers::expect_eq("trim", btrfsbackup::wizard::trim_text("  value \n"), "value");
    test_helpers::expect_true("bool true", btrfsbackup::wizard::parse_bool(" yes "), "yes should parse as true");
    test_helpers::expect_true("bool false", !btrfsbackup::wizard::parse_bool("OFF"), "OFF should parse as false");
    test_helpers::expect_eq("uint", std::to_string(btrfsbackup::wizard::parse_uint(" 42 ")), "42");
    test_helpers::expect_validation_error("invalid bool", [] {
        (void)btrfsbackup::wizard::parse_bool("maybe");
    }, "enter true or false");
    test_helpers::expect_validation_error("invalid uint", [] {
        (void)btrfsbackup::wizard::parse_uint("-1");
    }, "non-negative integer");
}

void test_prompt_defaults_and_retry() {
    std::istringstream input("\ninvalid\nfalse\nbad\n17\n");
    std::ostringstream output;

    test_helpers::expect_eq("prompt default", btrfsbackup::wizard::prompt_value(input, output, "Name", "default"), "default");
    test_helpers::expect_true("prompt bool retry", !btrfsbackup::wizard::prompt_bool(input, output, "Enabled", true), "false should be accepted after retry");
    test_helpers::expect_eq("prompt uint retry", std::to_string(btrfsbackup::wizard::prompt_uint(input, output, "Count", 3)), "17");
    test_helpers::expect_contains("prompt retry output", output.str(), "enter true or false");
    test_helpers::expect_contains("prompt uint retry output", output.str(), "enter a non-negative integer");
}

void test_source_names() {
    test_helpers::expect_eq("root source name", btrfsbackup::wizard::source_name_from_path("/"), "root");
    test_helpers::expect_eq("home source name", btrfsbackup::wizard::source_name_from_path("/home"), "home");
    test_helpers::expect_eq("sanitized source name", btrfsbackup::wizard::source_name_from_path("/mnt/My Data"), "My-Data");
    test_helpers::expect_eq("prefixed source name", btrfsbackup::wizard::source_name_from_path("/mnt/-data"), "source--data");
}

void test_source_selection() {
    std::vector<std::string> candidates{"/", "/home", "/srv"};
    test_helpers::expect_eq("default source selection", btrfsbackup::wizard::default_source_selection(candidates), "1,2");
    test_helpers::expect_eq("fallback source selection", btrfsbackup::wizard::default_source_selection({"/srv"}), "1");

    auto all = btrfsbackup::wizard::selected_sources_from_input(candidates, "a");
    test_helpers::expect_eq("all source count", std::to_string(all.size()), "3");

    auto selected = btrfsbackup::wizard::selected_sources_from_input(candidates, "2, 1,2");
    test_helpers::expect_eq("selected source count", std::to_string(selected.size()), "2");
    test_helpers::expect_eq("selected first", selected.at(0), "/home");
    test_helpers::expect_eq("selected second", selected.at(1), "/");

    test_helpers::expect_validation_error("source selection token", [&] {
        (void)btrfsbackup::wizard::selected_sources_from_input(candidates, "x");
    }, "invalid source selection");
    test_helpers::expect_validation_error("source selection range", [&] {
        (void)btrfsbackup::wizard::selected_sources_from_input(candidates, "4");
    }, "out of range");
}

void test_output_dir_validation() {
    test_helpers::expect_validation_error("unsafe root", [] {
        btrfsbackup::wizard::assert_safe_output_dir("/");
    }, "refusing unsafe output directory");
    test_helpers::expect_validation_error("unsafe etc", [] {
        btrfsbackup::wizard::assert_safe_output_dir("/etc/../etc");
    }, "refusing unsafe output directory");
    btrfsbackup::wizard::assert_safe_output_dir("/tmp/btrfs-backup-generated");
}

} // namespace

int main() {
    test_prompt_parsing();
    test_prompt_defaults_and_retry();
    test_source_names();
    test_source_selection();
    test_output_dir_validation();

    return test_helpers::finish("profile wizard tests");
}
