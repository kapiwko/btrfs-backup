# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-validation-tests validation_tests.cpp validation-tests btrfsbackup-config-model)
add_btrfsbackup_test(btrfsbackup-application-config-tests application_config_tests.cpp application-config-tests btrfsbackup-config)
add_btrfsbackup_test(btrfsbackup-profile-tests profile_tests.cpp profile-tests btrfsbackup-config)
add_btrfsbackup_test(btrfsbackup-profile-wizard-tests profile_wizard_tests.cpp profile-wizard-tests btrfsbackup-config)
add_btrfsbackup_test(btrfsbackup-application-services-tests application_services_tests.cpp application-services-tests btrfsbackup-config btrfsbackup-state)
