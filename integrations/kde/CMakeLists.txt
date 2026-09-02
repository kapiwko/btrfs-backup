# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
set(QT_DEFAULT_MAJOR_VERSION 6)
set(CMAKE_AUTOMOC ON)

include(KDECompilerSettings NO_POLICY_SCOPE)
set(KDE_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
set(KDE_INSTALL_APPDIR "${CMAKE_INSTALL_DATADIR}/applications")
set(KDE_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}")
set(KDE_INSTALL_LOCALEDIR "${CMAKE_INSTALL_DATADIR}/locale")

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml Quick)
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n JobWidgets KCMUtils Notifications Package KIO Runner WidgetsAddons)
find_package(Plasma REQUIRED)
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME KDEIntegration)
ki18n_install(plasmoid/po)
ki18n_install(kcm/po)

qt_add_dbus_interface(
    manager_client_sources
    "${PROJECT_SOURCE_DIR}/data/dbus/io.github.btrfsbackup.Manager1.xml"
    Manager1Interface
)
add_library(
    btrfsbackup_kde_manager_client STATIC
    ${manager_client_sources}
    client/ManagerApi.cpp
    client/ManagerApi.hpp
    client/TargetStatusParser.cpp
    client/TargetStatusParser.hpp
)
target_compile_features(btrfsbackup_kde_manager_client PUBLIC cxx_std_23)
target_include_directories(
    btrfsbackup_kde_manager_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/client
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(
    btrfsbackup_kde_manager_client
    PUBLIC
        btrfsbackup-manager-protocol
        btrfsbackup-status-documents
        Qt6::Core
        Qt6::DBus
)

add_library(kio_btrfsbackup MODULE
    kio/BtrfsBackupWorker.cpp
    kio/BtrfsBackupWorker.hpp
    kio/SecureBrowsePath.cpp
    kio/SecureBrowsePath.hpp
    kio/btrfsbackup.json
)
set_target_properties(kio_btrfsbackup PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/kf6/kio"
    PREFIX ""
)
install(TARGETS kio_btrfsbackup LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/kf6/kio")
target_compile_features(kio_btrfsbackup PRIVATE cxx_std_23)
target_include_directories(kio_btrfsbackup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client ${CMAKE_CURRENT_SOURCE_DIR}/kio)
target_link_libraries(kio_btrfsbackup PRIVATE
    btrfsbackup_kde_manager_client
    btrfsbackup-restore
    btrfsbackup-platform-linux-storage
    KF6::KIOCore
    Qt6::Core
    Qt6::DBus
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kio_btrfsbackup PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
endif()

add_library(previousversionsaction MODULE
    dolphin/PreviousVersionsAction.cpp
    dolphin/PreviousVersionsAction.hpp
    dolphin/PreviousVersionsSelection.cpp
    dolphin/PreviousVersionsSelection.hpp
    dolphin/previousversionsaction.json
)
set_target_properties(previousversionsaction PROPERTIES PREFIX "")
target_compile_features(previousversionsaction PRIVATE cxx_std_23)
target_include_directories(previousversionsaction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client ${CMAKE_CURRENT_SOURCE_DIR}/dolphin)
target_link_libraries(previousversionsaction PRIVATE
    btrfsbackup_kde_manager_client
    KF6::CoreAddons
    KF6::I18n
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::WidgetsAddons
    Qt6::Core
    Qt6::DBus
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(previousversionsaction PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS previousversionsaction LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/kf6/kfileitemaction")

qt_add_executable(btrfs-backup-kde-restore
    restore/main.cpp
    restore/RestoreController.cpp
    restore/RestoreController.hpp
    restore/RestoreJob.cpp
    restore/RestoreJob.hpp
)
qt_add_qml_module(btrfs-backup-kde-restore
    URI org.btrfsbackup.restore
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES
        restore/Main.qml
        restore/RestorePage.qml
)
target_compile_features(btrfs-backup-kde-restore PRIVATE cxx_std_23)
target_include_directories(btrfs-backup-kde-restore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client ${CMAKE_CURRENT_SOURCE_DIR}/restore)
target_link_libraries(btrfs-backup-kde-restore PRIVATE
    btrfsbackup_kde_manager_client
    btrfsbackup-restore
    btrfsbackup-platform-linux-restore
    btrfsbackup-platform-linux-storage
    KF6::I18n
    KF6::I18nQml
    KF6::JobWidgets
    KF6::Notifications
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfs-backup-kde-restore PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
endif()
install(TARGETS btrfs-backup-kde-restore RUNTIME DESTINATION "${KDE_INSTALL_BINDIR}")
install(FILES restore/btrfs-backup-kde-restore.notifyrc DESTINATION "${CMAKE_INSTALL_DATADIR}/knotifications6")

add_library(btrfsbackup-runner MODULE
    krunner/BackupRunner.cpp
    krunner/BackupRunner.hpp
    krunner/RunnerCommandParser.cpp
    krunner/RunnerCommandParser.hpp
    krunner/backuprunner.json
)
set_target_properties(btrfsbackup-runner PROPERTIES PREFIX "")
target_compile_features(btrfsbackup-runner PRIVATE cxx_std_23)
target_include_directories(btrfsbackup-runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client ${CMAKE_CURRENT_SOURCE_DIR}/krunner)
target_link_libraries(btrfsbackup-runner PRIVATE
    btrfsbackup_kde_manager_client
    KF6::I18n
    KF6::Runner
    Qt6::Core
    Qt6::DBus
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-runner PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS btrfsbackup-runner LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/kf6/krunner")

add_executable(
    btrfs-backup-kde-monitor
    monitor/CancellationRequestDispatcher.cpp
    monitor/CancellationRequestDispatcher.hpp
    monitor/NotificationDeduplicator.cpp
    monitor/NotificationDeduplicator.hpp
    monitor/BackupProgressJob.cpp
    monitor/BackupProgressJob.hpp
    monitor/BackupProgressMonitor.cpp
    monitor/BackupProgressMonitor.hpp
    monitor/TerminalNotificationService.cpp
    monitor/TerminalNotificationService.hpp
    monitor/main.cpp
)

if(BUILD_README_SCREENSHOTS)
    qt_add_executable(btrfs-backup-kde-screenshot-demo EXCLUDE_FROM_ALL
        monitor/BackupProgressJob.cpp
        monitor/BackupProgressJob.hpp
        ../../tools/screenshots/kde-desktop-demo.cpp
    )
    target_compile_features(btrfs-backup-kde-screenshot-demo PRIVATE cxx_std_23)
    target_include_directories(
        btrfs-backup-kde-screenshot-demo
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    )
    target_link_libraries(
        btrfs-backup-kde-screenshot-demo
        PRIVATE
            Qt6::Gui
            KF6::CoreAddons
            KF6::I18n
            KF6::JobWidgets
            KF6::Notifications
    )
endif()
target_compile_features(btrfs-backup-kde-monitor PRIVATE cxx_std_23)
target_compile_definitions(
    btrfs-backup-kde-monitor
    PRIVATE BTRFS_BACKUP_VERSION="${BTRFS_BACKUP_VERSION}"
)
target_link_libraries(
    btrfs-backup-kde-monitor
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        btrfsbackup_kde_manager_client
        KF6::CoreAddons
        KF6::I18n
        KF6::JobWidgets
        KF6::Notifications
)
install(TARGETS btrfs-backup-kde-monitor RUNTIME DESTINATION "${KDE_INSTALL_BINDIR}")
install(
    FILES monitor/io.github.btrfsbackup.ProgressMonitor.desktop
    DESTINATION "${KDE_INSTALL_APPDIR}"
)

add_subdirectory(kcm)
install(
    FILES monitor/btrfs-backup-kde-monitor.notifyrc
    DESTINATION "${CMAKE_INSTALL_DATADIR}/knotifications6"
)
configure_file(
    systemd/btrfs-backup-kde-monitor.service.in
    btrfs-backup-kde-monitor.service
    @ONLY
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backup-kde-monitor.service"
    DESTINATION lib/systemd/user
)
install(CODE [[
    set(wants_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/systemd/user/graphical-session.target.wants")
    file(MAKE_DIRECTORY "${wants_dir}")
    file(CREATE_LINK
        "../btrfs-backup-kde-monitor.service"
        "${wants_dir}/btrfs-backup-kde-monitor.service"
        SYMBOLIC
    )
]])

qt_add_qml_module(btrfsbackup_plasma_backend
    URI org.btrfsbackup.plasma
    VERSION 1.0
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma
    SOURCES
        plasmoid/src/BackupHistoryModel.cpp
        plasmoid/src/BackupHistoryModel.hpp
        plasmoid/src/BackupStatusModel.cpp
        plasmoid/src/BackupStatusModel.hpp
        plasmoid/src/ByteFormatting.cpp
        plasmoid/src/ByteFormatting.hpp
        plasmoid/src/RunStatusModel.cpp
        plasmoid/src/RunStatusModel.hpp
        plasmoid/src/TargetStatusModel.cpp
        plasmoid/src/TargetStatusModel.hpp
)

target_compile_features(btrfsbackup_plasma_backend PRIVATE cxx_std_23)
target_include_directories(btrfsbackup_plasma_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/src)
target_link_libraries(btrfsbackup_plasma_backend
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
        KF6::I18n
        btrfsbackup_kde_manager_client
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup_kde_manager_client PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backup-kde-monitor PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup_plasma_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(
    BTRFSBACKUP_PLASMA_QMLDIR
    "lib/qt6/qml/org/btrfsbackup/plasma"
    CACHE PATH
    "Install path for the btrfs-backup Plasma QML module"
)
set_target_properties(
    btrfsbackup_plasma_backendplugin
    PROPERTIES PREFIX "lib"
)
set_target_properties(
    btrfsbackup_plasma_backend
    btrfsbackup_plasma_backendplugin
    PROPERTIES INSTALL_RPATH "$ORIGIN"
)
install(
    TARGETS
        btrfsbackup_plasma_backend
        btrfsbackup_plasma_backendplugin
    LIBRARY DESTINATION ${BTRFSBACKUP_PLASMA_QMLDIR}
)
install(
    FILES
        ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma/qmldir
        ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma/btrfsbackup_plasma_backend.qmltypes
    DESTINATION ${BTRFSBACKUP_PLASMA_QMLDIR}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/package/metadata.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/metadata.json"
    @ONLY
)
set(
    BTRFSBACKUP_PLASMA_PACKAGE_INSTALL_DIR
    "${KDE_INSTALL_DATADIR}/plasma/plasmoids/org.btrfsbackup.plasmoid"
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/package/"
    DESTINATION "${BTRFSBACKUP_PLASMA_PACKAGE_INSTALL_DIR}"
    PATTERN "metadata.json.in" EXCLUDE
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/metadata.json"
    DESTINATION "${BTRFSBACKUP_PLASMA_PACKAGE_INSTALL_DIR}"
)

add_custom_target(btrfs-backup-kde)
add_dependencies(
    btrfs-backup-kde
    btrfs-backup-kde-monitor
    btrfsbackup_plasma_backendplugin
    kcm_btrfsbackup
    kio_btrfsbackup
    previousversionsaction
    btrfs-backup-kde-restore
    btrfsbackup-runner
    pofiles
    tsfiles
)

if(BUILD_TESTING)
    add_executable(krunner-command-parser-tests
        krunner/RunnerCommandParser.cpp
        krunner/tests/RunnerCommandParserTests.cpp
    )
    target_compile_features(krunner-command-parser-tests PRIVATE cxx_std_23)
    target_include_directories(krunner-command-parser-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/krunner)
    target_link_libraries(krunner-command-parser-tests PRIVATE Qt6::Core)
    add_test(NAME krunner-command-parser COMMAND krunner-command-parser-tests)
    set_tests_properties(krunner-command-parser PROPERTIES LABELS "kde;krunner")

    add_executable(dolphin-previous-versions-selection-tests
        dolphin/PreviousVersionsSelection.cpp
        dolphin/tests/PreviousVersionsSelectionTests.cpp
    )
    target_compile_features(dolphin-previous-versions-selection-tests PRIVATE cxx_std_23)
    target_include_directories(dolphin-previous-versions-selection-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dolphin)
    target_link_libraries(dolphin-previous-versions-selection-tests PRIVATE Qt6::Core)
    add_test(NAME dolphin-previous-versions-selection COMMAND dolphin-previous-versions-selection-tests)
    set_tests_properties(dolphin-previous-versions-selection PROPERTIES LABELS "kde;dolphin")

    add_executable(kio-btrfsbackup-metadata-tests kio/tests/PluginMetadataTests.cpp)
    target_compile_features(kio-btrfsbackup-metadata-tests PRIVATE cxx_std_23)
    target_compile_definitions(kio-btrfsbackup-metadata-tests PRIVATE
        BTRFSBACKUP_KIO_PLUGIN="$<TARGET_FILE:kio_btrfsbackup>"
    )
    target_link_libraries(kio-btrfsbackup-metadata-tests PRIVATE Qt6::Core)
    add_dependencies(kio-btrfsbackup-metadata-tests kio_btrfsbackup)
    add_test(NAME kio-btrfsbackup-metadata COMMAND kio-btrfsbackup-metadata-tests)
    set_tests_properties(kio-btrfsbackup-metadata PROPERTIES LABELS "kde;kio")

    add_executable(kio-secure-browse-path-tests
        kio/SecureBrowsePath.cpp
        kio/SecureBrowsePath.hpp
        kio/tests/SecureBrowsePathTests.cpp
    )
    target_compile_features(kio-secure-browse-path-tests PRIVATE cxx_std_23)
    target_include_directories(kio-secure-browse-path-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/kio)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(kio-secure-browse-path-tests PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME kio-secure-browse-path COMMAND kio-secure-browse-path-tests)
    set_tests_properties(kio-secure-browse-path PROPERTIES LABELS "kde;kio")

    add_executable(
        kio-snapshot-interop-probe
        spikes/KioSnapshotInteropProbe.cpp
    )
    target_compile_features(kio-snapshot-interop-probe PRIVATE cxx_std_23)
    target_link_libraries(kio-snapshot-interop-probe PRIVATE Qt6::Core)
    add_test(NAME kio-snapshot-interop-probe COMMAND kio-snapshot-interop-probe)
    set_tests_properties(kio-snapshot-interop-probe PROPERTIES LABELS kde)

    add_executable(
        kde-manager-api-tests
        client/tests/ManagerApiTests.cpp
    )
    target_compile_features(kde-manager-api-tests PRIVATE cxx_std_23)
    target_link_libraries(kde-manager-api-tests PRIVATE btrfsbackup_kde_manager_client Qt6::Core)
    add_test(NAME kde-manager-api COMMAND kde-manager-api-tests)
    set_tests_properties(kde-manager-api PROPERTIES LABELS kde)

    add_executable(
        plasma-target-status-model-tests
        plasmoid/src/ByteFormatting.cpp
        plasmoid/src/ByteFormatting.hpp
        plasmoid/src/TargetStatusModel.cpp
        plasmoid/src/TargetStatusModel.hpp
        plasmoid/tests/TargetStatusModelTests.cpp
    )
    target_compile_features(plasma-target-status-model-tests PRIVATE cxx_std_23)
    target_include_directories(
        plasma-target-status-model-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/src
    )
    target_link_libraries(
        plasma-target-status-model-tests
        PRIVATE
            Qt6::Core
            btrfsbackup_kde_manager_client
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(plasma-target-status-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME plasma-target-status-model COMMAND plasma-target-status-model-tests)
    set_tests_properties(plasma-target-status-model PROPERTIES LABELS kde)

    add_executable(
        plasma-status-models-tests
        plasmoid/src/BackupHistoryModel.cpp
        plasmoid/src/BackupHistoryModel.hpp
        plasmoid/src/ByteFormatting.cpp
        plasmoid/src/ByteFormatting.hpp
        plasmoid/src/RunStatusModel.cpp
        plasmoid/src/RunStatusModel.hpp
        plasmoid/tests/StatusModelsTests.cpp
    )
    target_compile_features(plasma-status-models-tests PRIVATE cxx_std_23)
    target_include_directories(
        plasma-status-models-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/src
    )
    target_link_libraries(
        plasma-status-models-tests
        PRIVATE
            Qt6::Core
            btrfsbackup_kde_manager_client
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(plasma-status-models-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME plasma-status-models COMMAND plasma-status-models-tests)
    set_tests_properties(plasma-status-models PROPERTIES LABELS kde)

    add_executable(
        plasma-progress-job-tests
        monitor/BackupProgressJob.cpp
        monitor/BackupProgressJob.hpp
        monitor/tests/BackupProgressJobTests.cpp
    )
    target_compile_features(plasma-progress-job-tests PRIVATE cxx_std_23)
    target_include_directories(
        plasma-progress-job-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    )
    target_link_libraries(
        plasma-progress-job-tests
        PRIVATE
            Qt6::Core
            btrfsbackup_kde_manager_client
            KF6::CoreAddons
            KF6::I18n
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(plasma-progress-job-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME plasma-progress-job COMMAND plasma-progress-job-tests)
    set_tests_properties(plasma-progress-job PROPERTIES LABELS kde)

    add_executable(
        kde-terminal-notification-tests
        monitor/NotificationDeduplicator.cpp
        monitor/NotificationDeduplicator.hpp
        monitor/TerminalNotificationService.cpp
        monitor/TerminalNotificationService.hpp
        monitor/tests/TerminalNotificationServiceTests.cpp
    )
    target_compile_features(kde-terminal-notification-tests PRIVATE cxx_std_23)
    target_include_directories(
        kde-terminal-notification-tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    )
    target_link_libraries(
        kde-terminal-notification-tests
        PRIVATE
            Qt6::Core
            KF6::I18n
            KF6::Notifications
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(kde-terminal-notification-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME kde-terminal-notification COMMAND kde-terminal-notification-tests)
    set_tests_properties(kde-terminal-notification PROPERTIES LABELS kde)

    add_custom_target(btrfs-backup-kde-tests)
    add_dependencies(
        btrfs-backup-kde-tests
        kde-manager-api-tests
        kde-terminal-notification-tests
        kio-secure-browse-path-tests
        kio-snapshot-interop-probe
        plasma-progress-job-tests
        plasma-status-models-tests
        plasma-target-status-model-tests
    )

    find_program(
        QML_EXECUTABLE
        NAMES qml
        PATHS /usr/lib/qt6/bin
        NO_DEFAULT_PATH
    )
    find_program(QML_EXECUTABLE NAMES qml)
    find_program(
        QML_TEST_RUNNER_EXECUTABLE
        NAMES qmltestrunner
        PATHS /usr/lib/qt6/bin
        NO_DEFAULT_PATH
    )
    find_program(QML_TEST_RUNNER_EXECUTABLE NAMES qmltestrunner)
    if(QML_EXECUTABLE)
        add_test(
            NAME plasma-backend-qml-smoke
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tests/backend-smoke.qml
        )
        add_test(
            NAME plasma-transfer-rate
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tests/transfer-rate.qml
        )
        add_test(
            NAME plasma-config-qml-smoke
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tests/config-smoke.qml
        )
        add_test(
            NAME kde-kcm-qml-smoke
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/kcm/tests/kcm-smoke.qml
        )
        add_test(
            NAME kde-kcm-new-profile-adoption
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/kcm/tests/new-profile-adoption.qml
        )
        add_test(
            NAME kde-kcm-new-profile-free-space
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/kcm/tests/new-profile-free-space.qml
        )
        add_test(
            NAME plasma-target-storage-usage
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                QT_QUICK_CONTROLS_STYLE=Basic
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tests/target-storage-usage.qml
        )
        set_tests_properties(
            plasma-backend-qml-smoke
            kde-kcm-qml-smoke
            kde-kcm-new-profile-adoption
            kde-kcm-new-profile-free-space
            plasma-config-qml-smoke
            plasma-target-storage-usage
            plasma-transfer-rate
            PROPERTIES
                LABELS kde
                TIMEOUT 10
        )
    endif()
    if(QML_TEST_RUNNER_EXECUTABLE)
        add_test(
            NAME kde-kcm-profile-unlocking-layout
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_TEST_RUNNER_EXECUTABLE}
                -input ${CMAKE_CURRENT_SOURCE_DIR}/kcm/tests/profile-unlocking-layout.qml
                -import ${CMAKE_BINARY_DIR}
                -o -,txt
        )
        add_test(
            NAME kde-kcm-profile-sources-actions
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_TEST_RUNNER_EXECUTABLE}
                -input ${CMAKE_CURRENT_SOURCE_DIR}/kcm/tests/profile-sources-actions.qml
                -import ${CMAKE_BINARY_DIR}
                -o -,txt
        )
        set_tests_properties(
            kde-kcm-profile-unlocking-layout
            kde-kcm-profile-sources-actions
            PROPERTIES
                LABELS kde
                TIMEOUT 10
        )
    endif()
endif()
