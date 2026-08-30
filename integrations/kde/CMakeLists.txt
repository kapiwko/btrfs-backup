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
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n JobWidgets KCMUtils Notifications Package KIO)
find_package(Plasma REQUIRED)
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

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

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
    pofiles
    tsfiles
)

if(BUILD_TESTING)
    add_executable(kio-btrfsbackup-metadata-tests kio/tests/PluginMetadataTests.cpp)
    target_compile_features(kio-btrfsbackup-metadata-tests PRIVATE cxx_std_23)
    target_compile_definitions(kio-btrfsbackup-metadata-tests PRIVATE
        BTRFSBACKUP_KIO_PLUGIN="$<TARGET_FILE:kio_btrfsbackup>"
    )
    target_link_libraries(kio-btrfsbackup-metadata-tests PRIVATE Qt6::Core)
    add_dependencies(kio-btrfsbackup-metadata-tests kio_btrfsbackup)
    add_test(NAME kio-btrfsbackup-metadata COMMAND kio-btrfsbackup-metadata-tests)
    set_tests_properties(kio-btrfsbackup-metadata PROPERTIES LABELS "kde;kio")

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
            NAME plasma-transfer-speed-chart
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tests/transfer-speed-chart.qml
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
            plasma-config-qml-smoke
            plasma-target-storage-usage
            plasma-transfer-speed-chart
            PROPERTIES
                LABELS kde
                TIMEOUT 10
        )
    endif()
endif()
