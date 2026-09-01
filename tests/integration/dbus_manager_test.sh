#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail

DAEMON="${1:?daemon path is required}"
DBUS_DAEMON="${2:?dbus-daemon path is required}"
BUSCTL="${3:?busctl path is required}"
POLICY_FILE="${4:?D-Bus policy path is required}"
POLKIT_AUTHORITY="${5:?fake polkit authority path is required}"
QML_EXECUTABLE="${6:-}"
QML_IMPORT_PATH="${7:-}"
QML_DBUS_TEST="${8:-}"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-dbus.XXXXXX)"
BUS_ADDRESS="unix:path=$TEST_ROOT/bus"
BUS_PID=""
DAEMON_PID=""
POLKIT_PID=""
SERVICE=io.github.btrfsbackup.Manager1
OBJECT=/io/github/btrfsbackup/Manager1
INTERFACE=io.github.btrfsbackup.Manager1

cleanup() {
    set +e
    [[ -n "$DAEMON_PID" ]] && kill "$DAEMON_PID" 2>/dev/null
    [[ -n "$DAEMON_PID" ]] && wait "$DAEMON_PID" 2>/dev/null
    [[ -n "$POLKIT_PID" ]] && kill "$POLKIT_PID" 2>/dev/null
    [[ -n "$POLKIT_PID" ]] && wait "$POLKIT_PID" 2>/dev/null
    [[ -n "$BUS_PID" ]] && kill "$BUS_PID" 2>/dev/null
    rm -rf -- "$TEST_ROOT"
}

start_polkit() {
    "$POLKIT_AUTHORITY" "$BUS_ADDRESS" "$TEST_ROOT/polkit.log" 500 &
    POLKIT_PID=$!
    for _ in {1..50}; do
        if "$BUSCTL" --address="$BUS_ADDRESS" --timeout=1 list 2>/dev/null \
            | grep -Fq 'org.freedesktop.PolicyKit1'; then
            return
        fi
        sleep 0.05
    done
    fail 'fake polkit authority did not acquire its bus name'
}

stop_polkit() {
    if [[ -n "$POLKIT_PID" ]]; then
        kill "$POLKIT_PID" 2>/dev/null || true
        wait "$POLKIT_PID" 2>/dev/null || true
        POLKIT_PID=""
    fi
}
trap cleanup EXIT

fail() {
    printf 'not ok - %s\n' "$1" >&2
    [[ -f "$TEST_ROOT/daemon.log" ]] && tail -n 40 "$TEST_ROOT/daemon.log" >&2
    exit 1
}

start_daemon() {
    "$DAEMON" \
        --bus-address "$BUS_ADDRESS" \
        --config-root "$TEST_ROOT/etc" \
        --public-profile-root "$TEST_ROOT/public" \
        --status-root "$TEST_ROOT/status" \
        --history-root "$TEST_ROOT/history" \
        --target-mount-root "$TEST_ROOT/mnt" \
        --udev-root "$TEST_ROOT/udev" \
        --systemd-root "$TEST_ROOT/systemd" \
        --skip-configuration-activation \
        --audit-log "$TEST_ROOT/audit/manager.jsonl" \
        >"$TEST_ROOT/daemon.log" 2>&1 &
    DAEMON_PID=$!
    for _ in {1..50}; do
        if "$BUSCTL" --address="$BUS_ADDRESS" --timeout=1 list 2>/dev/null | grep -Fq "$SERVICE"; then
            return
        fi
        kill -0 "$DAEMON_PID" 2>/dev/null || {
            cat "$TEST_ROOT/daemon.log" >&2
            fail 'daemon exited before acquiring its bus name'
        }
        sleep 0.05
    done
    fail 'daemon did not acquire its bus name'
}

stop_daemon() {
    if [[ -n "$DAEMON_PID" ]]; then
        kill "$DAEMON_PID"
        wait "$DAEMON_PID"
        DAEMON_PID=""
    fi
}

crash_daemon() {
    if [[ -n "$DAEMON_PID" ]]; then
        kill -KILL "$DAEMON_PID"
        wait "$DAEMON_PID" 2>/dev/null || true
        DAEMON_PID=""
    fi
}

stop_bus() {
    if [[ -n "$BUS_PID" ]]; then
        kill "$BUS_PID"
        BUS_PID=""
    fi
}

call() {
    "$BUSCTL" --address="$BUS_ADDRESS" --timeout=2 call "$SERVICE" "$OBJECT" "$INTERFACE" "$@"
}

install -d -m0755 \
    "$TEST_ROOT/etc/profiles/default" \
    "$TEST_ROOT/public" \
    "$TEST_ROOT/status/default" \
    "$TEST_ROOT/history/default" \
    "$TEST_ROOT/state/profiles/default" \
    "$TEST_ROOT/mnt" \
    "$TEST_ROOT/mapper"

cat >"$TEST_ROOT/public/default.json" <<'EOF_PUBLIC'
{"schemaVersion":1,"profileId":"default","name":"Default backup","target":{"name":"Backup disk"},"sources":[{"id":"home","name":"Home"}]}
EOF_PUBLIC
cat >"$TEST_ROOT/etc/btrfs-backup.conf" <<EOF_CONFIG
CONFIG_VERSION=1
STATE_ROOT=$TEST_ROOT/state
EOF_CONFIG
cat >"$TEST_ROOT/status/default/current.json" <<'EOF_STATUS'
{"schemaVersion":3,"runId":"20260829T160000Z-1-1","state":"running","phase":"sizing","activity":"sizing","canCancel":true,"errorCode":"","sourceName":"Home","targetName":"Backup disk","speedBps":10,"etaSeconds":20,"sourceProgress":30,"overallProgress":40,"progressAccuracy":"estimated","sourceIndex":1,"sourceCount":1,"startedAt":"2026-08-29T15:59:00Z","updatedAt":"2026-08-29T16:00:00Z"}
EOF_STATUS
cat >"$TEST_ROOT/history/default/20260825T100000Z-1-1.json" <<'EOF_HISTORY'
{"schemaVersion":2,"profileId":"default","profileName":"Default backup","runId":"20260825T100000Z-1-1","state":"failed","phase":"failed","message":"private","currentSourceName":"Home","targetName":"Backup disk","sourceIndex":1,"sourceCount":1,"startedAt":"2026-08-25T09:59:00Z","updatedAt":"2026-08-25T10:00:00Z","finishedAt":"2026-08-25T10:00:00Z","errorCode":"private.failure","errorMessage":"private failure","details":{"device":"/dev/private"},"recoverable":false,"suggestedAction":"","canCancel":false,"bytesProcessed":40,"bytesTotalEstimated":100,"runBytesProcessed":40,"speedBps":0,"etaSeconds":-1,"sourceProgress":40,"overallProgress":40,"progressAccuracy":"exact","exitCode":1}
EOF_HISTORY
cp "$TEST_ROOT/history/default/20260825T100000Z-1-1.json" "$TEST_ROOT/history/default/last.json"
cat >"$TEST_ROOT/state/profiles/default/last-success" <<'EOF_LAST_SUCCESS'
date=2026-08-24
timestamp=2026-08-24T18:42:00+0000
EOF_LAST_SUCCESS
cat >"$TEST_ROOT/etc/profiles/default/profile.json" <<'EOF_PROFILE'
{"schemaVersion":4,"configurationGeneration":"0123456789abcdef0123456789abcdef","profileId":"default","name":"Default backup","enabled":true,"target":{"device":"/dev/null","luksUuid":"11111111-2222-3333-4444-555555555555","btrfsUuid":"66666666-7777-8888-9999-aaaaaaaaaaaa","mapperName":"backupdisk","activation":{"mode":"askPassword"}},"sources":[{"id":"home","name":"Home","enabled":true,"subvolume":"/home","localSnapshotDir":"/.snapshots/home","remoteSubdir":"home","remoteRetention":2,"localRetention":2}]}
EOF_PROFILE
chmod 0644 "$TEST_ROOT/public/default.json" "$TEST_ROOT/status/default/current.json"
chmod 0600 "$TEST_ROOT/history/default/"*.json \
    "$TEST_ROOT/state/profiles/default/last-success" \
    "$TEST_ROOT/etc/profiles/default/profile.json"

cat >"$TEST_ROOT/test-bus.conf" <<EOF_TEST_BUS
<busconfig>
  <type>system</type>
  <listen>$BUS_ADDRESS</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
  </policy>
  <policy user="$(id -un)">
    <allow own="$SERVICE"/>
    <allow own="org.freedesktop.PolicyKit1"/>
  </policy>
</busconfig>
EOF_TEST_BUS
BUS_PID="$($DBUS_DAEMON --config-file="$TEST_ROOT/test-bus.conf" --fork --print-pid=1)"
start_polkit
start_daemon

capabilities="$(call GetCapabilities)"
grep -Fq 'readOnly' <<<"$capabilities" || fail 'capabilities omit readOnly'
grep -Fq 'start-backup' <<<"$capabilities" || fail 'capabilities omit operational control'
grep -Fq 'change-signals' <<<"$capabilities" || fail 'capabilities omit change signals'
profiles="$(call ListProfiles)"
grep -Fq 'Default backup' <<<"$profiles" || fail 'public profile was not returned'
status_before="$(call GetStatus s default)"
grep -Fq 'state' <<<"$status_before" || fail 'current status omits state'
grep -Fq 'running' <<<"$status_before" || fail 'current state was not returned'
if [[ -n "$QML_EXECUTABLE" ]]; then
    DBUS_SYSTEM_BUS_ADDRESS="$BUS_ADDRESS" \
        QT_QPA_PLATFORM=offscreen \
        QT_FORCE_STDERR_LOGGING=1 \
        "$QML_EXECUTABLE" -I "$QML_IMPORT_PATH" "$QML_DBUS_TEST" \
        >"$TEST_ROOT/qml.log" 2>&1 &
    qml_pid=$!
    for _ in {1..220}; do
        grep -Fq 'initial-manager-state-ready' "$TEST_ROOT/qml.log" && break
        kill -0 "$qml_pid" 2>/dev/null || {
            cat "$TEST_ROOT/qml.log" >&2
            fail 'Plasma backend exited before loading the initial manager state'
        }
        sleep 0.05
    done
    if ! grep -Fq 'initial-manager-state-ready' "$TEST_ROOT/qml.log"; then
        cat "$TEST_ROOT/qml.log" >&2
        fail 'Plasma backend did not load the initial manager state'
    fi
    cp "$TEST_ROOT/status/default/current.json" "$TEST_ROOT/status/default/current.json.running"
    cat >"$TEST_ROOT/status/default/current.json.next" <<'EOF_COMPLETED_STATUS'
{"schemaVersion":3,"runId":"20260829T160000Z-1-1","state":"succeeded","phase":"completed","activity":"idle","canCancel":false,"errorCode":"","sourceName":"Home","targetName":"Backup disk","speedBps":0,"etaSeconds":-1,"sourceProgress":100,"overallProgress":100,"progressAccuracy":"exact","sourceIndex":1,"sourceCount":1,"startedAt":"2026-08-29T15:59:00Z","updatedAt":"2026-08-29T16:00:00Z"}
EOF_COMPLETED_STATUS
    mv "$TEST_ROOT/status/default/current.json.next" "$TEST_ROOT/status/default/current.json"
    wait "$qml_pid" || {
        cat "$TEST_ROOT/qml.log" >&2
        fail 'Plasma backend did not consume the manager change signal'
    }
    mv "$TEST_ROOT/status/default/current.json.running" "$TEST_ROOT/status/default/current.json"
fi
history="$(call GetHistorySanitized suu default 0 1)"
grep -Fq 'backup.failed' <<<"$history" || fail 'history error was not sanitized'
if grep -Fq '/dev/private' <<<"$history"; then fail 'private history details crossed the bus'; fi
device="$(call GetDeviceState s default)"
grep -Fq 'connected' <<<"$device" || fail 'device state was not returned'
if grep -Fq '/dev/null' <<<"$device"; then fail 'device path crossed the bus'; fi

introspection="$($BUSCTL --address="$BUS_ADDRESS" introspect "$SERVICE" "$OBJECT" "$INTERFACE")"
for method in GetCapabilities ListProfiles GetStatus GetHistorySanitized GetDeviceState StartBackup CancelBackup ValidateTarget EjectTarget GetProfileDetails UpdateProfileSettings AddProfileSource UpdateProfileSource RemoveProfileSource DeleteProfile SetProfileEnabled ListTargetCredentials AddTargetPassphrase AddTargetKey GenerateTargetKey RemoveTargetCredential ListProvisioningDevices ListSourceCandidates StartDevicePreparation GetDevicePreparation CancelDevicePreparation; do
    grep -Fq "$method" <<<"$introspection" || fail "missing method $method"
done
for signal in ProfilesChanged StatusChanged HistoryChanged DeviceStateChanged; do
    grep -Fq "$signal" <<<"$introspection" || fail "missing signal $signal"
done

profile_details="$(call GetProfileDetails s default)"
grep -Fq 'fingerprint' <<<"$profile_details" || fail 'profile details omit fingerprint'
grep -Fq 'generation' <<<"$profile_details" || fail 'profile details omit generation'
if grep -Fq 'key contents' <<<"$profile_details"; then fail 'secret contents crossed the unauthenticated details method'; fi

fingerprint="$(grep -oE '[0-9a-f]{64}' <<<"$profile_details")"
if [[ ! "$fingerprint" =~ ^[0-9a-f]{64}$ ]]; then
    printf 'profile details response: %s\n' "$profile_details" >&2
    fail 'profile details returned an invalid fingerprint'
fi
touch "$TEST_ROOT/polkit.log.allow"
settings_request='{"name":"Edited backup","dailyLimit":false,"autoEject":true}'
saved="$(call UpdateProfileSettings ssss default "0123456789abcdef0123456789abcdef" "$fingerprint" "$settings_request")"
grep -Fq 'generation' <<<"$saved" || fail 'profile save omitted the new generation'
grep -Fq 'Edited backup' "$TEST_ROOT/etc/profiles/default/profile.json" || fail 'profile settings update was not published'
grep -Eq '"configurationGeneration"[[:space:]]*:[[:space:]]*"[0-9a-f]{32}"' "$TEST_ROOT/etc/profiles/default/profile.json" \
    || fail 'profile save did not assign a generation'
rm -f -- "$TEST_ROOT/polkit.log.allow"

"$BUSCTL" --address="$BUS_ADDRESS" --timeout=2 wait \
    "$SERVICE" "$OBJECT" "$INTERFACE" ProfilesChanged >"$TEST_ROOT/profiles-signal" &
profiles_signal_pid=$!
"$BUSCTL" --address="$BUS_ADDRESS" --timeout=2 wait \
    "$SERVICE" "$OBJECT" "$INTERFACE" StatusChanged >"$TEST_ROOT/status-signal" &
status_signal_pid=$!
"$BUSCTL" --address="$BUS_ADDRESS" --timeout=2 wait \
    "$SERVICE" "$OBJECT" "$INTERFACE" HistoryChanged >"$TEST_ROOT/history-signal" &
history_signal_pid=$!
"$BUSCTL" --address="$BUS_ADDRESS" --timeout=2 wait \
    "$SERVICE" "$OBJECT" "$INTERFACE" DeviceStateChanged >"$TEST_ROOT/device-signal" &
device_signal_pid=$!
sleep 0.1
cp "$TEST_ROOT/public/default.json" "$TEST_ROOT/public/default.json.next"
mv "$TEST_ROOT/public/default.json.next" "$TEST_ROOT/public/default.json"
cp "$TEST_ROOT/status/default/current.json" "$TEST_ROOT/status/default/current.json.next"
mv "$TEST_ROOT/status/default/current.json.next" "$TEST_ROOT/status/default/current.json"
cp "$TEST_ROOT/history/default/last.json" "$TEST_ROOT/history/default/last.json.next"
mv "$TEST_ROOT/history/default/last.json.next" "$TEST_ROOT/history/default/last.json"
wait "$profiles_signal_pid" || fail 'profile filesystem change did not emit ProfilesChanged'
wait "$status_signal_pid" || fail 'status filesystem change did not emit StatusChanged'
wait "$history_signal_pid" || fail 'history filesystem change did not emit HistoryChanged'
wait "$device_signal_pid" || fail 'status filesystem change did not emit DeviceStateChanged'

set +e
call StartBackup s default >/dev/null 2>&1
call ValidateTarget s default >/dev/null 2>&1
set -e
grep -Fq 'io.github.btrfsbackup.start-backup' "$TEST_ROOT/polkit.log" \
    || fail 'start request used the wrong polkit action'
grep -Fq 'io.github.btrfsbackup.validate-target' "$TEST_ROOT/polkit.log" \
    || fail 'validate request used the wrong polkit action'
grep -Fq 'io.github.btrfsbackup.manage-profile-configuration' "$TEST_ROOT/polkit.log" \
    || fail 'profile update used the wrong polkit action'
grep -Fq '"callerUid":'"$(id -u)" "$TEST_ROOT/audit/manager.jsonl" \
    || fail 'manager audit omitted the D-Bus caller UID'
grep -Fq '"action":"start-backup"' "$TEST_ROOT/audit/manager.jsonl" \
    || fail 'manager audit omitted the requested action'
grep -Fq '"profileId":"default"' "$TEST_ROOT/audit/manager.jsonl" \
    || fail 'manager audit omitted the profile'
grep -Fq '"result":"denied"' "$TEST_ROOT/audit/manager.jsonl" \
    || fail 'manager audit omitted the denied result'
grep -Fq '"errorCode":"io.github.btrfsbackup.Error.NotAuthorized"' "$TEST_ROOT/audit/manager.jsonl" \
    || fail 'manager audit omitted the stable denial code'
[[ "$(stat -c '%a' "$TEST_ROOT/audit/manager.jsonl")" == "600" ]] \
    || fail 'manager audit is not root-only'
unique_callers="$(awk '{print $1}' "$TEST_ROOT/polkit.log" | sort -u | wc -l)"
[[ "$unique_callers" -ge 2 ]] || fail 'authorization was not bound to each caller connection'

authorization_count="$(wc -l < "$TEST_ROOT/polkit.log")"
daemon_log_lines="$(wc -l < "$TEST_ROOT/daemon.log")"
touch "$TEST_ROOT/polkit.log.allow"
call StartBackup s default >/dev/null 2>&1 &
disconnected_caller_pid=$!
for _ in {1..50}; do
    [[ "$(wc -l < "$TEST_ROOT/polkit.log")" -gt "$authorization_count" ]] && break
    sleep 0.02
done
kill "$disconnected_caller_pid" 2>/dev/null || true
wait "$disconnected_caller_pid" 2>/dev/null || true
rm -f -- "$TEST_ROOT/polkit.log.allow"
for _ in {1..50}; do
    if tail -n "+$((daemon_log_lines + 1))" "$TEST_ROOT/daemon.log" \
        | grep -Fq 'manager operation was not authorized'; then
        break
    fi
    sleep 0.02
done
tail -n "+$((daemon_log_lines + 1))" "$TEST_ROOT/daemon.log" \
    | grep -Fq 'manager operation was not authorized' \
    || fail 'caller disconnect during authorization reached the operational backend'

set +e
invalid_request_output="$(call GetStatus s '../invalid' 2>&1)"
invalid_request_status=$?
set -e
[[ "$invalid_request_status" -ne 0 ]] || fail 'malformed profile id was accepted'
grep -Fq 'manager request is invalid' <<<"$invalid_request_output" \
    || fail "malformed profile id returned an unsafe error: $invalid_request_output"
if call GetHistorySanitized suu default 0 101 >/dev/null 2>&1; then fail 'unbounded history limit was accepted'; fi
cp "$TEST_ROOT/status/default/current.json" "$TEST_ROOT/status/default/current.json.valid"
printf '%s\n' '{invalid' > "$TEST_ROOT/status/default/current.json"
chmod 0644 "$TEST_ROOT/status/default/current.json"
set +e
malformed_output="$(call GetStatus s default 2>&1)"
malformed_status=$?
set -e
[[ "$malformed_status" -ne 0 ]] || fail 'malformed status document was accepted'
grep -Fq 'manager request is invalid' <<<"$malformed_output" \
    || fail "malformed status returned an unsafe error: $malformed_output"
if grep -Fq "$TEST_ROOT" <<<"$malformed_output"; then
    fail 'private manager path leaked through a D-Bus error'
fi
mv "$TEST_ROOT/status/default/current.json.valid" "$TEST_ROOT/status/default/current.json"
call ListProfiles >/dev/null
kill -0 "$DAEMON_PID" || fail 'caller disconnect stopped the daemon'

crash_daemon
mv "$TEST_ROOT/status" "$TEST_ROOT/status.saved"
start_daemon
"$BUSCTL" --address="$BUS_ADDRESS" --timeout=2 wait \
    "$SERVICE" "$OBJECT" "$INTERFACE" StatusChanged >"$TEST_ROOT/recreated-status-signal" &
recreated_status_signal_pid=$!
sleep 0.1
mkdir -p "$TEST_ROOT/status/default"
mv "$TEST_ROOT/status.saved/default/current.json" "$TEST_ROOT/status/default/current.json"
wait "$recreated_status_signal_pid" \
    || fail 'creating a previously absent status root did not emit StatusChanged'
status_after="$(call GetStatus s default)"
[[ "$status_before" == "$status_after" ]] || fail 'daemon crash recovery did not restore visible state'

stop_daemon
stop_polkit
stop_bus
rm -f -- "$TEST_ROOT/bus"
cat >"$TEST_ROOT/policy-bus.conf" <<EOF_POLICY
<busconfig>
  <type>system</type>
  <listen>$BUS_ADDRESS</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <allow send_destination="org.freedesktop.DBus"/>
    <allow receive_sender="org.freedesktop.DBus"/>
    <allow send_requested_reply="true"/>
    <allow receive_requested_reply="true"/>
  </policy>
  <policy user="$(id -un)">
    <allow own="$SERVICE"/>
    <allow own="org.freedesktop.PolicyKit1"/>
  </policy>
  <include>$POLICY_FILE</include>
</busconfig>
EOF_POLICY
BUS_PID="$($DBUS_DAEMON --config-file="$TEST_ROOT/policy-bus.conf" --fork --print-pid=1)"
start_polkit
start_daemon
call GetCapabilities >/dev/null || fail 'policy denied an allowed read method'
call GetProfileDetails s default >/dev/null || fail 'policy denied unauthenticated profile details'
set +e
polkit_output="$(call StartBackup s default 2>&1)"
polkit_status=$?
set -e
[[ "$polkit_status" -ne 0 ]] || fail 'test start unexpectedly succeeded'
grep -Fq 'operation is not authorized' <<<"$polkit_output" \
    || fail "operational call did not pass through bus policy and polkit: $polkit_output"

set +e
call ListTargetCredentials s default >/dev/null 2>&1
credentials_status=$?
call ListProvisioningDevices >/dev/null 2>&1
devices_status=$?
call ListSourceCandidates >/dev/null 2>&1
sources_status=$?
call GetDevicePreparation s guessed-operation >/dev/null 2>&1
preparation_status=$?
call CancelDevicePreparation s guessed-operation >/dev/null 2>&1
cancel_preparation_status=$?
set -e
[[ "$credentials_status" -ne 0 ]] || fail 'credential metadata was available without authorization'
[[ "$devices_status" -ne 0 ]] || fail 'device inventory was available without authorization'
[[ "$sources_status" -ne 0 ]] || fail 'source paths were available without authorization'
[[ "$preparation_status" -ne 0 ]] || fail 'foreign preparation status was available without authorization'
[[ "$cancel_preparation_status" -ne 0 ]] || fail 'foreign preparation cancellation was accepted without authorization'
grep -Fq 'io.github.btrfsbackup.manage-target-credentials' "$TEST_ROOT/polkit.log" \
    || fail 'credential listing used the wrong polkit action'
grep -Fq 'io.github.btrfsbackup.prepare-backup-device' "$TEST_ROOT/polkit.log" \
    || fail 'device inspection used the wrong polkit action'

printf '%s\n' 'ok - private D-Bus manager API'
