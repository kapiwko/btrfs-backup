# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(btrfsbackup-validation-tests ValidationTests.cpp validation-tests btrfsbackup-config-domain)
add_btrfsbackup_test(btrfsbackup-repository-path-tests RepositoryPathTests.cpp repository-path-tests btrfsbackup-config-domain)
add_btrfsbackup_test(btrfsbackup-storage-policy-tests StoragePolicyTests.cpp storage-policy-tests btrfsbackup-config-domain)
add_btrfsbackup_test(btrfsbackup-target-identity-tests TargetIdentityTests.cpp target-identity-tests btrfsbackup-config-domain)
add_btrfsbackup_test(btrfsbackup-application-config-tests ApplicationConfigTests.cpp application-config-tests btrfsbackup-platform-linux-config)
add_btrfsbackup_test(btrfsbackup-profile-tests ProfileTests.cpp profile-tests btrfsbackup-platform-linux-config btrfsbackup-config-json btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-profile-wizard-tests ProfileWizardTests.cpp profile-wizard-tests btrfsbackup-platform-linux-config btrfsbackup-config-wizard btrfsbackup-platform-linux)
add_btrfsbackup_test(btrfsbackup-application-services-tests ApplicationServicesTests.cpp application-services-tests btrfsbackup-platform-linux-config btrfsbackup-config-json btrfsbackup-platform-linux btrfsbackup-state)
