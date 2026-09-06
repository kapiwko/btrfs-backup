# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-cli STATIC
    BackupTool.cpp
    CtlTool.cpp
    InstallationCommand.cpp
    profile/ProfileCommand.cpp
    profile/ProfileCreateCommand.cpp
    profile/ProfileListCommand.cpp
    profile/ProfileWizardCommand.cpp
    repository/RepositoryCommand.cpp
    runner/RunnerCommand.cpp
    runner/RunnerComposition.cpp
    runner/RunnerJsonCodec.cpp
    runner/RunnerOptions.cpp
    runner/RunnerPresenter.cpp
    restore/RestoreCommand.cpp
    status/StatusCommand.cpp
    status/StatusHistoryCommand.cpp
    status/StatusShowCommand.cpp
    target/TargetCommand.cpp
    target/TargetService.cpp
)

target_compile_features(btrfsbackup-cli PUBLIC cxx_std_23)
target_compile_definitions(
    btrfsbackup-cli
    PRIVATE
        BTRFSBACKUP_SYSTEMD_CRYPTSETUP="${BTRFSBACKUP_SYSTEMD_CRYPTSETUP_EXECUTABLE}"
        BTRFS_BACKUP_VERSION="${BTRFS_BACKUP_VERSION}"
)
target_restricted_include_directories(btrfsbackup-cli
    PUBLIC_HEADERS
        cli/BackupTool.hpp
        cli/CtlTool.hpp
        cli/InstallationCommand.hpp
        cli/profile/ProfileCommand.hpp
        cli/profile/ProfileCreateCommand.hpp
        cli/profile/ProfileListCommand.hpp
        cli/profile/ProfileWizardCommand.hpp
        cli/repository/RepositoryCommand.hpp
        cli/runner/RunnerCommand.hpp
        cli/runner/RunnerOptions.hpp
        cli/runner/RunnerPresenter.hpp
        cli/restore/RestoreCommand.hpp
        cli/status/StatusCommand.hpp
        cli/status/StatusHistoryCommand.hpp
        cli/status/StatusShowCommand.hpp
        cli/target/TargetCommand.hpp
        cli/target/TargetService.hpp
    PRIVATE_HEADERS
        cli/runner/RunnerComposition.hpp
        cli/runner/RunnerJsonCodec.hpp
)
target_link_libraries(btrfsbackup-cli
    PUBLIC
        btrfsbackup-backup
    PRIVATE
        btrfsbackup-backup-model
        btrfsbackup-backup-planning
        btrfsbackup-backup-ports
        btrfsbackup-config-domain
        btrfsbackup-config-json
        btrfsbackup-config-ports
        btrfsbackup-core
        btrfsbackup-platform-linux
        btrfsbackup-platform-linux-config
        btrfsbackup-restore
        btrfsbackup-state
        btrfsbackup-transfer
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
