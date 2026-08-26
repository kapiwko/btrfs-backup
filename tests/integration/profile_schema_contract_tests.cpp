// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/validation_test_helpers.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <config/json.hpp>
#include <config/json_io.hpp>
#include <config/profile.hpp>

namespace fs = std::filesystem;

namespace {

using btrfsbackup::Json;

fs::path repo_root() {
    return BTRFSBACKUP_SOURCE_DIR;
}

bool array_contains(const Json& array, const std::string& value) {
    if (!array.is_array()) {
        return false;
    }
    for (const Json& item : array) {
        if (item.is_string() && item.get<std::string>() == value) {
            return true;
        }
    }
    return false;
}

void expect_required(const std::string& name, const Json& object, const std::string& field) {
    test_helpers::expect_true(name, array_contains(object.at("required"), field), "schema does not require " + field);
}

void expect_no_additional_properties(const std::string& name, const Json& object) {
    test_helpers::expect_true(name, object.contains("additionalProperties"), "missing additionalProperties");
    test_helpers::expect_true(name, object.at("additionalProperties") == false, "additionalProperties should be false");
}

Json schema() {
    return btrfsbackup::load_json_file(repo_root() / "data" / "schemas" / "profile.schema.json");
}

Json example_profile() {
    return btrfsbackup::load_json_file(repo_root() / "data" / "examples" / "profile.example.json");
}

void test_schema_requires_cpp_required_fields() {
    Json root = schema();

    expect_no_additional_properties("schema top additional", root);
    test_helpers::expect_true("schema version", root.at("properties").at("schemaVersion").at("const") == 3, "profile schema must be version 3");
    expect_required("schema top schemaVersion", root, "schemaVersion");
    expect_required("schema top profileId", root, "profileId");
    expect_required("schema top target", root, "target");
    expect_required("schema top sources", root, "sources");
    test_helpers::expect_true("schema no notifications", !root.at("properties").contains("notifications"), "canonical schema must not expose desktop notification policy");
    test_helpers::expect_eq(
        "schema configuration generation pattern",
        root.at("properties").at("configurationGeneration").at("pattern").get<std::string>(),
        "^[0-9a-f]{32}$"
    );

    const Json& target = root.at("properties").at("target");
    expect_no_additional_properties("schema target additional", target);
    expect_required("schema target device", target, "device");
    expect_required("schema target luks uuid", target, "luksUuid");
    expect_required("schema target btrfs uuid", target, "btrfsUuid");
    expect_required("schema target mapper", target, "mapperName");
    test_helpers::expect_true("schema hides target mount point", !target.at("properties").contains("mountPoint"), "mount point must be application-controlled");
    test_helpers::expect_true("schema hides target mount unit", !target.at("properties").contains("mountUnit"), "mount unit must be application-controlled");

    const Json& paths = root.at("properties").at("paths");
    expect_no_additional_properties("schema paths additional", paths);
    expect_required("schema paths remote", paths, "remoteRoot");
    expect_required("schema paths incoming", paths, "incomingRoot");
    for (const std::string& key : {"sourcesDir", "stateDir", "statusRoot", "historyRoot"}) {
        test_helpers::expect_true(
            "schema hides system path " + key,
            !paths.at("properties").contains(key),
            "system paths must not be configurable profile properties"
        );
    }

    const Json& settings = root.at("properties").at("settings");
    expect_no_additional_properties("schema settings additional", settings);
    expect_required("schema settings remote retention", settings, "remoteRetention");
    expect_required("schema settings local retention", settings, "localRetention");
    test_helpers::expect_true(
        "schema retention max",
        root.at("$defs").at("retentionCount").at("maximum") == 100000,
        "retention maximum should match C++ validator"
    );
    test_helpers::expect_true(
        "schema byte max",
        root.at("$defs").at("byteCount").at("maximum") == 1000000000000000LL,
        "byte count maximum should match C++ validator"
    );

    const Json& hooks = root.at("properties").at("hooks");
    expect_no_additional_properties("schema hooks additional", hooks);
    test_helpers::expect_true("schema before hooks", hooks.at("properties").contains("beforeSnapshot"), "schema should document beforeSnapshot hooks");
    test_helpers::expect_true("schema after hooks", hooks.at("properties").contains("afterSnapshot"), "schema should document afterSnapshot hooks");
    const Json& hook_list = root.at("$defs").at("hookList");
    test_helpers::expect_true("schema hook max", hook_list.at("maxItems") == 64, "hook max should match C++ validator");
    const Json& hook = hook_list.at("items");
    expect_no_additional_properties("schema hook additional", hook);
    expect_required("schema hook type", hook, "type");
    expect_required("schema hook program", hook, "program");
    expect_required("schema hook arguments", hook, "arguments");
    expect_required("schema hook timeout", hook, "timeoutSeconds");
    test_helpers::expect_true("schema hook type const", hook.at("properties").at("type").at("const") == "program", "only program hooks should be supported");
    test_helpers::expect_eq(
        "schema hook program pattern",
        hook.at("properties").at("program").at("pattern").get<std::string>(),
        "^/etc/btrfs-backup/hooks\\.d/[^/]+$"
    );
    test_helpers::expect_true("schema hook arg max", hook.at("properties").at("arguments").at("maxItems") == 128, "hook argument max should match C++ validator");
    test_helpers::expect_true("schema hook timeout min", hook.at("properties").at("timeoutSeconds").at("minimum") == 1, "hook timeout minimum should match C++ validator");
    test_helpers::expect_true("schema hook timeout max", hook.at("properties").at("timeoutSeconds").at("maximum") == 86400, "hook timeout maximum should match C++ validator");

    const Json& source = root.at("properties").at("sources").at("items");
    expect_no_additional_properties("schema source additional", source);
    expect_required("schema source id", source, "id");
    expect_required("schema source subvolume", source, "subvolume");
    expect_required("schema source local", source, "localSnapshotDir");
    expect_required("schema source remote", source, "remoteSubdir");
    test_helpers::expect_true("schema source max", root.at("properties").at("sources").at("maxItems") == 128, "source max should match C++ validator");
}

void test_example_profile_matches_cpp_validator() {
    Json normalized = btrfsbackup::normalize_profile(example_profile());
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(normalized);

    test_helpers::expect_true("example profile id", normalized.at("profileId") == "default", "wrong example profile id");
    test_helpers::expect_true("example mount point", profile.target.mount_point == "/mnt/btrfs-backup/default", "wrong derived mount point");
    test_helpers::expect_true("example mount unit", profile.target.mount_unit == "mnt-btrfs\\x2dbackup-default.mount", "wrong derived mount unit");
    test_helpers::expect_true("example source count", normalized.at("sources").size() == 1, "wrong example source count");
}

void test_cpp_validator_rejects_schema_rejected_fields() {
    Json top = example_profile();
    top["unexpected"] = true;
    test_helpers::expect_validation_error("unknown top field", [&] {
        (void)btrfsbackup::normalize_profile(top);
    }, "profile.unexpected");

    Json target = example_profile();
    target["target"]["unexpected"] = true;
    test_helpers::expect_validation_error("unknown target field", [&] {
        (void)btrfsbackup::normalize_profile(target);
    }, "target.unexpected");

    Json source = example_profile();
    source["sources"].at(0)["unexpected"] = true;
    test_helpers::expect_validation_error("unknown source field", [&] {
        (void)btrfsbackup::normalize_profile(source);
    }, "sources[0].unexpected");
}

void test_schema_and_cpp_require_btrfs_uuid() {
    Json root = schema();
    expect_required("schema requires btrfs uuid", root.at("properties").at("target"), "btrfsUuid");

    Json profile = example_profile();
    profile["target"].erase("btrfsUuid");
    test_helpers::expect_validation_error("cpp requires btrfs uuid", [&] {
        (void)btrfsbackup::normalize_profile(profile);
    }, "target.btrfsUuid");
}

} // namespace

int main() {
    test_schema_requires_cpp_required_fields();
    test_example_profile_matches_cpp_validator();
    test_cpp_validator_rejects_schema_rejected_fields();
    test_schema_and_cpp_require_btrfs_uuid();

    return test_helpers::finish("profile schema contract tests");
}
