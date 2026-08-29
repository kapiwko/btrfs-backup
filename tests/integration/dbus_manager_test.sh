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
    "$TEST_ROOT/mnt" \
    "$TEST_ROOT/mapper"

cat >"$TEST_ROOT/public/default.json" <<'EOF_PUBLIC'
{"schemaVersion":1,"profileId":"default","name":"Default backup","target":{"name":"Backup disk"},"sources":[{"id":"home","name":"Home"}]}
EOF_PUBLIC
cat >"$TEST_ROOT/status/default/current.json" <<'EOF_STATUS'
{"schemaVersion":3,"runId":"20260829T160000Z-1-1","state":"running","phase":"sizing","activity":"sizing","canCancel":true,"errorCode":"","sourceName":"Home","targetName":"Backup disk","speedBps":10,"etaSeconds":20,"sourceProgress":30,"overallProgress":40,"progressAccuracy":"estimated"}
EOF_STATUS
cat >"$TEST_ROOT/history/default/20260825T100000Z-1-1.json" <<'EOF_HISTORY'
{"schemaVersion":2,"profileId":"default","profileName":"Default backup","runId":"20260825T100000Z-1-1","state":"failed","phase":"failed","message":"private","currentSourceName":"Home","targetName":"Backup disk","finishedAt":"2026-08-25T10:00:00Z","errorCode":"private.failure","details":{"device":"/dev/private"},"overallProgress":40}
EOF_HISTORY
cp "$TEST_ROOT/history/default/20260825T100000Z-1-1.json" "$TEST_ROOT/history/default/last.json"
cat >"$TEST_ROOT/etc/profiles/default/profile.json" <<'EOF_PROFILE'
{"schemaVersion":3,"profileId":"default","name":"Default backup","enabled":true,"target":{"device":"/dev/null","luksUuid":"11111111-2222-3333-4444-555555555555","btrfsUuid":"66666666-7777-8888-9999-aaaaaaaaaaaa","mapperName":"backupdisk"},"sources":[{"id":"home","name":"Home","enabled":true,"subvolume":"/home","localSnapshotDir":"/.snapshots/home","remoteSubdir":"home","remoteRetention":2,"localRetention":2}]}
EOF_PROFILE
chmod 0644 "$TEST_ROOT/public/default.json" "$TEST_ROOT/status/default/current.json"
chmod 0600 "$TEST_ROOT/history/default/"*.json "$TEST_ROOT/etc/profiles/default/profile.json"

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
profiles="$(call ListProfiles)"
grep -Fq 'Default backup' <<<"$profiles" || fail 'public profile was not returned'
status_before="$(call GetStatus s default)"
grep -Fq 'state' <<<"$status_before" || fail 'current status omits state'
grep -Fq 'running' <<<"$status_before" || fail 'current state was not returned'
if [[ -n "$QML_EXECUTABLE" ]]; then
    DBUS_SYSTEM_BUS_ADDRESS="$BUS_ADDRESS" \
        QT_QPA_PLATFORM=offscreen \
        "$QML_EXECUTABLE" -I "$QML_IMPORT_PATH" "$QML_DBUS_TEST" \
        || fail 'Plasma backend did not consume the manager API'
fi
history="$(call GetHistorySanitized suu default 0 1)"
grep -Fq 'backup.failed' <<<"$history" || fail 'history error was not sanitized'
if grep -Fq '/dev/private' <<<"$history"; then fail 'private history details crossed the bus'; fi
device="$(call GetDeviceState s default)"
grep -Fq 'connected' <<<"$device" || fail 'device state was not returned'
if grep -Fq '/dev/null' <<<"$device"; then fail 'device path crossed the bus'; fi

introspection="$($BUSCTL --address="$BUS_ADDRESS" introspect "$SERVICE" "$OBJECT" "$INTERFACE")"
for method in GetCapabilities ListProfiles GetStatus GetHistorySanitized GetDeviceState StartBackup CancelBackup ValidateTarget EjectTarget; do
    grep -Fq "$method" <<<"$introspection" || fail "missing method $method"
done
if grep -Eq 'SaveProfile|DeleteProfile' <<<"$introspection"; then
    fail 'an unsupported mutating method is exported'
fi

set +e
call StartBackup s default >/dev/null 2>&1
call ValidateTarget s default >/dev/null 2>&1
set -e
grep -Fq 'io.github.btrfsbackup.start-backup' "$TEST_ROOT/polkit.log" \
    || fail 'start request used the wrong polkit action'
grep -Fq 'io.github.btrfsbackup.validate-target' "$TEST_ROOT/polkit.log" \
    || fail 'validate request used the wrong polkit action'
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
start_daemon
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
set +e
polkit_output="$(call StartBackup s default 2>&1)"
polkit_status=$?
set -e
[[ "$polkit_status" -ne 0 ]] || fail 'test start unexpectedly succeeded'
grep -Fq 'operation is not authorized' <<<"$polkit_output" \
    || fail "operational call did not pass through bus policy and polkit: $polkit_output"

printf '%s\n' 'ok - private D-Bus manager API'
