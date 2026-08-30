// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QUrl>

#include <iostream>

namespace {

bool upstream_url_can_represent(
    bool repository_is_mounted,
    bool source_and_snapshot_share_filesystem,
    bool numeric_subvolume_ids_are_known
) {
    return repository_is_mounted && source_and_snapshot_share_filesystem && numeric_subvolume_ids_are_known;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    const QUrl upstream{
        QStringLiteral("snapshot://01234567-89ab-cdef-0123-456789abcdef/subvolume/256/900/Documents/report.txt")
    };
    const QUrl repository{
        QStringLiteral("btrfsbackup:/repositories/repo-1/sessions/session-1/snapshots/snapshot-1/Documents/report.txt")
    };

    bool passed = true;
    passed &= expect(upstream.scheme() == QStringLiteral("snapshot"), "upstream scheme was not parsed");
    passed &= expect(
        upstream.host() == QStringLiteral("01234567-89ab-cdef-0123-456789abcdef"),
        "upstream URL does not bind its host to the filesystem UUID"
    );
    passed &= expect(
        upstream.path().startsWith(QStringLiteral("/subvolume/256/900/")),
        "upstream URL does not require origin and snapshot numeric subvolume IDs"
    );
    passed &= expect(
        upstream_url_can_represent(true, true, true),
        "local mounted snapshot topology should be representable"
    );
    passed &= expect(
        !upstream_url_can_represent(false, false, false),
        "detached repository was incorrectly representable by the upstream URL"
    );
    passed &= expect(repository.host().isEmpty(), "repository URL must not expose a device as its authority");
    passed &= expect(
        repository.path().contains(QStringLiteral("/sessions/session-1/")),
        "repository URL must bind browsing to an authorized session"
    );
    passed &= expect(
        !upstream.path().contains(QStringLiteral("session")),
        "upstream URL unexpectedly carries an authorization session"
    );
    return passed ? 0 : 1;
}
