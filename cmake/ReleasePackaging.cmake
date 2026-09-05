# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set(CPACK_PACKAGE_NAME "btrfs-backup")
set(CPACK_PACKAGE_VENDOR "btrfs-backup")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Verified Btrfs backups to an encrypted removable target"
)
set(CPACK_PACKAGE_CONTACT "Kamil Piwowarski <kapiwko@gmail.com>")
set(CPACK_PACKAGE_FILE_NAME "btrfs-backup-${PROJECT_VERSION}-install")
set(CPACK_PACKAGE_RELOCATABLE OFF)
set(CPACK_GENERATOR "TGZ")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_COMPONENT_UNSPECIFIED_DISPLAY_NAME "Base")
set(CPACK_COMPONENT_KDEINTEGRATION_DISPLAY_NAME "KDE integration")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_UNSPECIFIED_PACKAGE_NAME "btrfs-backup")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Kamil Piwowarski <kapiwko@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "btrfs-progs (>= 6.0), coreutils, cryptsetup, libacl1, libcryptsetup12, libfdisk1, libmount1, libstdc++6, libsystemd0, libudev1, polkitd, systemd, util-linux"
)
set(CPACK_DEBIAN_UNSPECIFIED_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/preinst"
)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_UNSPECIFIED_PACKAGE_NAME "btrfs-backup")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Daemons")
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
set(CPACK_RPM_SPEC_MORE_DEFINE
    "%define _buildhost reproducible\n%define use_source_date_epoch_as_buildtime 1\n%define clamp_mtime_to_source_date_epoch 1"
)
set(CPACK_RPM_PACKAGE_REQUIRES
    "acl-libs, btrfs-progs >= 6.0, coreutils, cryptsetup, libstdc++, polkit, systemd, systemd-libs, util-linux"
)
set(CPACK_RPM_UNSPECIFIED_PRE_INSTALL_SCRIPT_FILE
    "${CMAKE_SOURCE_DIR}/packaging/debian/preinst"
)
set(CPACK_COMPONENTS_ALL Unspecified)
if(BUILD_KDE_INTEGRATION)
    list(APPEND CPACK_COMPONENTS_ALL KDEIntegration)
endif()
include(CPack)
