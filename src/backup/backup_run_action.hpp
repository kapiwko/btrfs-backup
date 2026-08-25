// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace btrfsbackup {

enum class BackupRunActionKind {
    RecoverPending,
    CleanupIncoming,
    BeforeSnapshotHook,
    CreateSnapshot,
    AfterSnapshotHook,
    SelectParent,
    SendReceive,
    VerifyReceived,
    CommitReceived,
    ApplyRemoteRetention,
    ApplyLocalRetention,
    CleanupSource,
};

} // namespace btrfsbackup
