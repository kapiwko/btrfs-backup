// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace btrfsbackup::manager_protocol {

inline constexpr char service_name[] = "io.github.btrfsbackup.Manager1";
inline constexpr char object_path[] = "/io/github/btrfsbackup/Manager1";
inline constexpr char interface_name[] = "io.github.btrfsbackup.Manager1";

inline constexpr int api_major = 2;
inline constexpr int api_minor = 8;
inline constexpr int capabilities_schema_version = 1;
inline constexpr int profile_schema_version = 4;
inline constexpr int profile_summary_schema_version = 2;
inline constexpr int public_status_schema_version = 5;
inline constexpr int history_schema_version = 3;
inline constexpr int device_state_schema_version = 1;
inline constexpr int target_storage_schema_version = 1;
inline constexpr int operation_result_schema_version = 1;
inline constexpr int profile_details_schema_version = 2;
inline constexpr int browse_session_schema_version = 2;
inline constexpr int target_credentials_schema_version = 1;
inline constexpr int device_provisioning_schema_version = 3;
inline constexpr int storage_topology_schema_version = 2;
inline constexpr int device_preparation_plan_schema_version = 3;
inline constexpr int existing_target_inspection_schema_version = 3;

namespace feature {

inline constexpr char profiles[] = "profiles";
inline constexpr char status[] = "status";
inline constexpr char sanitized_history[] = "sanitized-history";
inline constexpr char device_state[] = "device-state";
inline constexpr char target_storage_usage[] = "target-storage-usage";
inline constexpr char start_backup[] = "start-backup";
inline constexpr char cancel_backup[] = "cancel-backup";
inline constexpr char validate_target[] = "validate-target";
inline constexpr char eject_target[] = "eject-target";
inline constexpr char change_signals[] = "change-signals";
inline constexpr char browse_backups[] = "browse-backups";
inline constexpr char profile_administration[] = "profile-administration";
inline constexpr char profile_details[] = "profile-details";
inline constexpr char profile_activation[] = "profile-activation";
inline constexpr char target_credentials[] = "target-credentials";
inline constexpr char device_provisioning[] = "device-provisioning";

} // namespace feature

namespace method {

inline constexpr char get_capabilities[] = "GetCapabilities";
inline constexpr char list_profiles[] = "ListProfiles";
inline constexpr char get_status[] = "GetStatus";
inline constexpr char get_history_sanitized[] = "GetHistorySanitized";
inline constexpr char get_device_state[] = "GetDeviceState";
inline constexpr char start_backup[] = "StartBackup";
inline constexpr char cancel_backup[] = "CancelBackup";
inline constexpr char validate_target[] = "ValidateTarget";
inline constexpr char eject_target[] = "EjectTarget";
inline constexpr char get_profile_details[] = "GetProfileDetails";
inline constexpr char update_profile_settings[] = "UpdateProfileSettings";
inline constexpr char add_profile_source[] = "AddProfileSource";
inline constexpr char update_profile_source[] = "UpdateProfileSource";
inline constexpr char remove_profile_source[] = "RemoveProfileSource";
inline constexpr char delete_profile[] = "DeleteProfile";
inline constexpr char set_profile_enabled[] = "SetProfileEnabled";
inline constexpr char open_browse_session[] = "OpenBrowseSession";
inline constexpr char renew_browse_session[] = "RenewBrowseSession";
inline constexpr char set_browse_session_active[] = "SetBrowseSessionActive";
inline constexpr char close_browse_session[] = "CloseBrowseSession";
inline constexpr char list_browse_directory[] = "ListBrowseDirectory";
inline constexpr char inspect_browse_entry[] = "InspectBrowseEntry";
inline constexpr char open_browse_file[] = "OpenBrowseFile";
inline constexpr char open_browse_root[] = "OpenBrowseRoot";
inline constexpr char inspect_browse_repository[] = "InspectBrowseRepository";
inline constexpr char resolve_backup_coverage[] = "ResolveBackupCoverage";
inline constexpr char list_target_credentials[] = "ListTargetCredentials";
inline constexpr char add_target_passphrase[] = "AddTargetPassphrase";
inline constexpr char add_target_key[] = "AddTargetKey";
inline constexpr char generate_target_key[] = "GenerateTargetKey";
inline constexpr char remove_target_credential[] = "RemoveTargetCredential";
inline constexpr char inspect_storage_topology[] = "InspectStorageTopology";
inline constexpr char inspect_existing_target[] = "InspectExistingTarget";
inline constexpr char build_device_preparation_plan[] = "BuildDevicePreparationPlan";
inline constexpr char list_source_candidates[] = "ListSourceCandidates";
inline constexpr char start_device_preparation[] = "StartDevicePreparation";
inline constexpr char get_device_preparation[] = "GetDevicePreparation";
inline constexpr char cancel_device_preparation[] = "CancelDevicePreparation";

} // namespace method

namespace signal {

inline constexpr char profiles_changed[] = "ProfilesChanged";
inline constexpr char status_changed[] = "StatusChanged";
inline constexpr char history_changed[] = "HistoryChanged";
inline constexpr char device_state_changed[] = "DeviceStateChanged";

} // namespace signal

} // namespace btrfsbackup::manager_protocol
