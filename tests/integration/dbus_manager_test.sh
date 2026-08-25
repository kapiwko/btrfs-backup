#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail

DAEMON="${1:?daemon path is required}"
DBUS_DAEMON="${2:?dbus-daemon path is required}"
BUSCTL="${3:?busctl path is required}"
POLICY_FILE="${4:?D-Bus policy path is required}"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-dbus.XXXXXX)"
BUS_ADDRESS="unix:path=$TEST_ROOT/bus"
BUS_PID=""
DAEMON_PID=""
SERVICE=io.github.btrfsbackup.Manager1
OBJECT=/io/github/btrfsbackup/Manager1
INTERFACE=io.github.btrfsbackup.Manager1

cleanup() {
    set +e
    [[ -n "$DAEMON_PID" ]] && kill "$DAEMON_PID" 2>/dev/null
    [[ -n "$DAEMON_PID" ]] && wait "$DAEMON_PID" 2>/dev/null
    [[ -n "$BUS_PID" ]] && kill "$BUS_PID" 2>/dev/null
    rm -rf -- "$TEST_ROOT"
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
{"schemaVersion":3,"state":"running","errorCode":"","sourceName":"Home","targetName":"Backup disk","speedBps":10,"etaSeconds":20,"sourceProgress":30,"overallProgress":40,"progressAccuracy":"estimated"}
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

BUS_PID="$($DBUS_DAEMON --session --fork --address="$BUS_ADDRESS" --print-pid=1)"
start_daemon

capabilities="$(call GetCapabilities)"
grep -Fq 'readOnly' <<<"$capabilities" || fail 'capabilities omit readOnly'
grep -Fq 'true' <<<"$capabilities" || fail 'capabilities do not identify a read-only manager'
profiles="$(call ListProfiles)"
grep -Fq 'Default backup' <<<"$profiles" || fail 'public profile was not returned'
status_before="$(call GetStatus s default)"
grep -Fq 'state' <<<"$status_before" || fail 'current status omits state'
grep -Fq 'running' <<<"$status_before" || fail 'current state was not returned'
history="$(call GetHistorySanitized suu default 0 1)"
grep -Fq 'backup.failed' <<<"$history" || fail 'history error was not sanitized'
if grep -Fq '/dev/private' <<<"$history"; then fail 'private history details crossed the bus'; fi
device="$(call GetDeviceState s default)"
grep -Fq 'connected' <<<"$device" || fail 'device state was not returned'
if grep -Fq '/dev/null' <<<"$device"; then fail 'device path crossed the bus'; fi

introspection="$($BUSCTL --address="$BUS_ADDRESS" introspect "$SERVICE" "$OBJECT" "$INTERFACE")"
for method in GetCapabilities ListProfiles GetStatus GetHistorySanitized GetDeviceState; do
    grep -Fq "$method" <<<"$introspection" || fail "missing method $method"
done
if grep -Eq 'StartBackup|CancelBackup|EjectTarget|SaveProfile|DeleteProfile' <<<"$introspection"; then
    fail 'a mutating method is exported'
fi

if call GetStatus s '../invalid' >/dev/null 2>&1; then fail 'malformed profile id was accepted'; fi
if call GetHistorySanitized suu default 0 101 >/dev/null 2>&1; then fail 'unbounded history limit was accepted'; fi
cp "$TEST_ROOT/status/default/current.json" "$TEST_ROOT/status/default/current.json.valid"
printf '%s\n' '{invalid' > "$TEST_ROOT/status/default/current.json"
chmod 0644 "$TEST_ROOT/status/default/current.json"
set +e
malformed_output="$(call GetStatus s default 2>&1)"
malformed_status=$?
set -e
[[ "$malformed_status" -ne 0 ]] || fail 'malformed status document was accepted'
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
  </policy>
  <include>$POLICY_FILE</include>
</busconfig>
EOF_POLICY
BUS_PID="$($DBUS_DAEMON --config-file="$TEST_ROOT/policy-bus.conf" --fork --print-pid=1)"
start_daemon
call GetCapabilities >/dev/null || fail 'policy denied an allowed read method'
set +e
denied_output="$(call StartBackup s default 2>&1)"
denied_status=$?
set -e
[[ "$denied_status" -ne 0 ]] || fail 'policy allowed an undeclared mutating method'
if ! grep -Eqi 'denied|rejected|not permitted' <<<"$denied_output"; then
    fail "undeclared call reached the service instead of being denied by policy: $denied_output"
fi

printf '%s\n' 'ok - private D-Bus manager API'
