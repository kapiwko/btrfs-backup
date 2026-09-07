// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/CallerCredentials.hpp>

#include <systemd/sd-bus.h>

#include <grp.h>
#include <sys/fsuid.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <memory>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::dbus::filesystem_access_identity_from_credentials;
using btrfsbackup::daemon::dbus::effective_uid_from_credentials;
using btrfsbackup::daemon::dbus::real_uid_from_credentials;

constexpr uid_t real_uid = 41001;
constexpr uid_t effective_uid = 0;
constexpr uid_t filesystem_uid = 41003;
constexpr gid_t real_gid = 42001;
constexpr gid_t effective_gid = 0;
constexpr gid_t supplementary_gid = 42003;
constexpr gid_t filesystem_gid = 42004;

class Child final {
  public:
    Child(pid_t pid, int release_descriptor) : pid_(pid), release_descriptor_(release_descriptor) {
    }

    ~Child() {
        if (release_descriptor_ >= 0)
            close(release_descriptor_);
        if (pid_ > 0) {
            int status = 0;
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        }
    }

    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

  private:
    pid_t pid_;
    int release_descriptor_;
};

void run_credential_child(int ready_descriptor, int release_descriptor) {
    const std::array groups{supplementary_gid};
    if (setgroups(groups.size(), groups.data()) != 0 ||
        setresgid(real_gid, effective_gid, effective_gid) != 0)
        _exit(2);
    setfsgid(filesystem_gid);
    if (setfsgid(-1) != filesystem_gid || setresuid(real_uid, effective_uid, effective_uid) != 0)
        _exit(2);
    setfsuid(filesystem_uid);
    if (setfsuid(-1) != filesystem_uid)
        _exit(2);
    constexpr char ready = 'R';
    if (write(ready_descriptor, &ready, 1) != 1)
        _exit(3);
    char release = 0;
    if (read(release_descriptor, &release, 1) < 0)
        _exit(4);
    _exit(0);
}

void test_effective_process_credentials_drive_filesystem_access() {
    if (geteuid() != 0) {
        std::cout << "skip - differing real/effective credential fixture requires root\n";
        return;
    }

    std::array<int, 2> ready_pipe{};
    std::array<int, 2> release_pipe{};
    if (pipe(ready_pipe.data()) != 0 || pipe(release_pipe.data()) != 0) {
        test_helpers::fail("create credential pipes", "pipe failed");
        return;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        test_helpers::fail("fork credential process", "fork failed");
        return;
    }
    if (pid == 0) {
        close(ready_pipe[0]);
        close(release_pipe[1]);
        run_credential_child(ready_pipe[1], release_pipe[0]);
    }
    close(ready_pipe[1]);
    close(release_pipe[0]);
    Child child(pid, release_pipe[1]);
    char ready = 0;
    const bool child_ready = read(ready_pipe[0], &ready, 1) == 1 && ready == 'R';
    close(ready_pipe[0]);
    if (!child_ready) {
        test_helpers::fail(
            "credential process became ready",
            "credential process could not install the fixture credentials"
        );
        return;
    }

    sd_bus_creds* raw_credentials = nullptr;
    const int result = sd_bus_creds_new_from_pid(
        &raw_credentials,
        pid,
        SD_BUS_CREDS_UID | SD_BUS_CREDS_EUID | SD_BUS_CREDS_FSUID |
            SD_BUS_CREDS_GID | SD_BUS_CREDS_EGID | SD_BUS_CREDS_FSGID |
            SD_BUS_CREDS_SUPPLEMENTARY_GIDS
    );
    if (result < 0) {
        test_helpers::fail("read process credentials", "sd-bus rejected process credentials");
        return;
    }
    std::unique_ptr<sd_bus_creds, decltype(&sd_bus_creds_unref)> credentials(
        raw_credentials,
        sd_bus_creds_unref
    );
    const auto identity = filesystem_access_identity_from_credentials(credentials.get());
    test_helpers::expect_true(
        "filesystem UID",
        identity.uid == filesystem_uid && identity.uid != effective_uid && identity.uid != real_uid,
        "filesystem UID was not preferred over the effective and real UIDs"
    );
    test_helpers::expect_true(
        "filesystem GID",
        std::ranges::find(identity.groups, filesystem_gid) != identity.groups.end() &&
            std::ranges::find(identity.groups, effective_gid) == identity.groups.end() &&
            std::ranges::find(identity.groups, real_gid) == identity.groups.end(),
        "filesystem GID was not preferred over the effective and real GIDs"
    );
    test_helpers::expect_true(
        "supplementary GID",
        std::ranges::find(identity.groups, supplementary_gid) != identity.groups.end(),
        "supplementary group was lost"
    );
    test_helpers::expect_true(
        "operation effective UID",
        effective_uid_from_credentials(credentials.get()) == effective_uid,
        "operation ownership did not use the effective UID"
    );
    test_helpers::expect_true(
        "audit real UID",
        real_uid_from_credentials(credentials.get()) == real_uid,
        "audit identity no longer uses the real UID"
    );
}

} // namespace

int main() {
    test_effective_process_credentials_drive_filesystem_access();
    return test_helpers::finish("D-Bus caller credential selection tests");
}
