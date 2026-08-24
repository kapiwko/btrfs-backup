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
