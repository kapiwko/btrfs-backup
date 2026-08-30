// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace btrfsbackup::manager_protocol {

inline constexpr char service_name[] = "io.github.btrfsbackup.Manager1";
inline constexpr char object_path[] = "/io/github/btrfsbackup/Manager1";
inline constexpr char interface_name[] = "io.github.btrfsbackup.Manager1";

inline constexpr int api_major = 1;
inline constexpr int api_minor = 1;
inline constexpr int capabilities_schema_version = 1;
inline constexpr int profile_schema_version = 3;
inline constexpr int profile_summary_schema_version = 1;
inline constexpr int public_status_schema_version = 3;
inline constexpr int history_schema_version = 1;
inline constexpr int device_state_schema_version = 1;
inline constexpr int operation_result_schema_version = 1;

namespace feature {

inline constexpr char profiles[] = "profiles";
inline constexpr char status[] = "status";
inline constexpr char sanitized_history[] = "sanitized-history";
inline constexpr char device_state[] = "device-state";
inline constexpr char start_backup[] = "start-backup";
inline constexpr char cancel_backup[] = "cancel-backup";
inline constexpr char validate_target[] = "validate-target";
inline constexpr char eject_target[] = "eject-target";
inline constexpr char change_signals[] = "change-signals";

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

} // namespace method

namespace signal {

inline constexpr char profiles_changed[] = "ProfilesChanged";
inline constexpr char status_changed[] = "StatusChanged";
inline constexpr char history_changed[] = "HistoryChanged";
inline constexpr char device_state_changed[] = "DeviceStateChanged";

} // namespace signal

} // namespace btrfsbackup::manager_protocol
