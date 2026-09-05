// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseSessionMountStore.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <set>

#include <config/json/JsonIo.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

using btrfsbackup::BrowseSessionId;
using btrfsbackup::daemon::control::BrowseSessionMountStore;

void test_marker_v1_roundtrip_and_cleanup_steps() {
    const fs::path root = test_helpers::test_root("browse-mount-store", "roundtrip");
    const auto owner = static_cast<std::uint32_t>(getuid());
    BrowseSessionMountStore store(root, owner);
    store.prepare_root();
    const BrowseSessionId id{"browse-one"};
    auto record = store.make_record(id, owner, "target-key", "target.mount", true);
    fs::create_directories(record.view);
    record.view_mounted = true;
    store.write(id, record);

    const auto document = btrfsbackup::config::json::load_json_file(record.marker);
    test_helpers::expect_true(
        "marker schema",
        document.at("schemaVersion") == 1 && document.at("sessionId") == "browse-one" &&
            document.at("directory") == record.directory.string() && document.at("view") == record.view.string(),
        "marker v1 fields or paths changed"
    );
    struct stat marker_status{};
    test_helpers::expect_true(
        "marker permissions",
        stat(record.marker.c_str(), &marker_status) == 0 && (marker_status.st_mode & 0777) == 0600,
        "marker is not private"
    );
    const auto decoded = store.read(record.marker);
    test_helpers::expect_true(
        "marker roundtrip",
        decoded.has_value() && decoded->target_key == record.target_key && decoded->target_unit == record.target_unit &&
            decoded->directory == record.directory && decoded->view == record.view && decoded->view_mounted &&
            decoded->target_mounted_by_backend && !decoded->target_released,
        "valid marker did not roundtrip"
    );

    const auto stale = store.stale_records({});
    const auto live = store.stale_records({"browse-one"});
    test_helpers::expect_true(
        "stale selection",
        stale.size() == 1 && stale.front().first.value() == "browse-one" && live.empty(),
        "stale enumeration ignored the live-session set"
    );
    store.remove_session_directory(record);
    test_helpers::expect_true("directory removal", !fs::exists(record.directory), "session directory was not removed");
    record.target_released = true;
    store.write(id, record);
    store.remove_marker(record);
    test_helpers::expect_true("marker removal", !fs::exists(record.marker), "cleanup marker was not removed");
    fs::remove_all(root);
}

void test_invalid_markers_are_ignored() {
    const fs::path root = test_helpers::test_root("browse-mount-store", "invalid");
    const auto owner = static_cast<std::uint32_t>(getuid());
    BrowseSessionMountStore store(root, owner);
    store.prepare_root();
    const fs::path state = root / ".state";
    test_helpers::write_file(state / "broken.json", "{");
    test_helpers::write_file(state / "schema.json", R"({"schemaVersion":2})");
    test_helpers::write_file(state / "mismatch.json", R"({"schemaVersion":1,"sessionId":"other","callerUid":1,"targetKey":"k","targetUnit":"u","directory":"/tmp/x","view":"/tmp/x/repository","viewMounted":false,"targetMountedByBackend":false,"targetReleased":false})");
    test_helpers::write_file(state / "escaped.json", btrfsbackup::config::json::dump_json({
                                                         {"schemaVersion", 1},
                                                         {"sessionId", "escaped"},
                                                         {"callerUid", owner},
                                                         {"targetKey", "k"},
                                                         {"targetUnit", "u"},
                                                         {"directory", (root / "outside").string()},
                                                         {"view", (root / "outside/repository").string()},
                                                         {"viewMounted", false},
                                                         {"targetMountedByBackend", false},
                                                         {"targetReleased", false},
                                                     }));
    fs::create_symlink(state / "broken.json", state / "link.json");

    test_helpers::expect_true("broken marker", !store.read(state / "broken.json"), "invalid JSON was accepted");
    test_helpers::expect_true("schema marker", !store.read(state / "schema.json"), "unknown marker schema was accepted");
    test_helpers::expect_true("identifier marker", !store.read(state / "mismatch.json"), "mismatched marker identifier was accepted");
    test_helpers::expect_true("path marker", !store.read(state / "escaped.json"), "marker path outside its UID/session root was accepted");
    test_helpers::expect_true("invalid stale markers", store.stale_records({}).empty(), "invalid or symlink marker entered stale cleanup");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_marker_v1_roundtrip_and_cleanup_steps();
    test_invalid_markers_are_ignored();
    return test_helpers::finish("browse session mount store tests");
}
