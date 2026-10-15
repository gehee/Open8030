#!/bin/sh
# SPDX-License-Identifier: MIT
# Ascent VRX RAM loading and restoration; invoked by ./open8030.
set -eu
PATH=/sbin:/bin:/usr/sbin:/usr/bin
export PATH
ACTION=${1:?Expected start, retain or stop}
STAGE=${2:?Expected a temporary stage directory}
WINDOW=${3:-300}
case "$STAGE" in /tmp/ar8030-zephyr-*) ;; *) exit 2 ;; esac
case "${STAGE#/tmp/}" in *[!A-Za-z0-9_-]*) exit 2 ;; esac

start() {
MODE=autonomous
WINDOW=${WINDOW:-300}
case "$WINDOW" in ''|*[!0-9]*) exit 2 ;; esac
test "${#WINDOW}" -le 3
test "$WINDOW" -ge 10
test "$WINDOW" -le 600
FALLBACK=$(expr "$WINDOW" + 55)
case "$STAGE" in /tmp/ar8030-zephyr-*) ;; *) exit 2 ;; esac
case "${STAGE#/tmp/}" in *[!A-Za-z0-9_-]*) exit 2 ;; esac
test -f "$STAGE/candidate.img"
test -f "$STAGE/vrx.sh"
test ! -e "$STAGE/in-test"
test ! -e "$STAGE/restored"
test ! -e "$STAGE/native-left-running"
test ! -e "$STAGE/keep-running"

test "$(sha256sum "$STAGE/candidate.img" | cut -d ' ' -f 1)" = b5417bc57b45d51b2827632382d931aa4d364aa8cd350904215e5f8d61c3f832
test "$(sha256sum "$STAGE/bb_config_gnd.json" | cut -d ' ' -f 1)" = 5cf0cd31645d4786c6b04399c6c609386d427ffc4c6d7f6e42d81e3e770c2a74
test "$(sha256sum /usrdata/fpv/boot_ar8030/bb_demo_gnd_3v3.img | cut -d ' ' -f 1)" = dc58247910af564376350404383c5c411cbec21663873fba24e70c61a3584e3c
GND=$(pidof ar_ldy_gnd)
DAEMON=$(pidof daemon_sdio)
GUI=$(pidof GlassesUI)
case "$GUI" in *' '*|'') exit 2 ;; esac
test "$(readlink /proc/$GUI/exe)" = /tmp/fpvdata/bin/GlassesUI
GUI_PARENT=$(awk '/^PPid:/{print $2}' /proc/$GUI/status)
test "$(readlink /proc/$GUI_PARENT/exe)" = /bin/busybox
case "$(tr '\000' ' ' < /proc/$GUI_PARENT/cmdline)" in
    '/bin/sh ./gui_run.sh 485 16 16 '|'sh ./gui_run.sh 485 16 16 ') ;;
    *) exit 2 ;;
esac
test "$(sha256sum /tmp/fpvdata/bin/GlassesUI | cut -d ' ' -f 1)" = 07185033c7a075374b32aebc5b3506814eddbbba6c66d011c73037445a1a77c2
test "$(sha256sum /usrdata/fpv/gui_run.sh | cut -d ' ' -f 1)" = d6cba209b085430e7af8493ba19cbd09b6d15c23ae8169acde4a68afaf549de5
case "$GND:$DAEMON" in *' '*|:*|*:) exit 2 ;; esac
test "$(readlink /proc/$GND/exe)" = /usrdata/fpv/ar_ldy_gnd
test "$(readlink /proc/$DAEMON/exe)" = /usrdata/fpv/daemon_sdio
test "$(sha256sum /usrdata/fpv/ar_ldy_gnd | cut -d ' ' -f 1)" = 243c11864d266caebdc76d9c4357ca6517fd1afdbbfcbaf9d6c59c113d6d6216
test "$(sha256sum /usrdata/fpv/daemon_sdio | cut -d ' ' -f 1)" = 63f16bb229a4091e3d5fc00537135a2f8bb692a3042ff27bde8009dd1bf5cefc
test "$(sha256sum /mod/ar_vb.ko | cut -d ' ' -f 1)" = 361a7dc11ebe261c6b9a3e5909ef8864f8645d1d1514df7c01ee48e41f110601
test "$(sha256sum /mod/ar_sys.ko | cut -d ' ' -f 1)" = 89f96f44987b3c65dbd8e69fa8a04c0738516a7fd4457563f638fa60ade673ee
cat /sys/bus/sdio/devices/*/uevent | busybox grep -q '^SDIO_ID=4152:8031$'
mkdir "$STAGE/start-lock"
sha256sum /usrdata/fpv/boot_ar8030/bb_demo_gnd_3v3.img > "$STAGE/stock.sha256"
cat /proc/sys/kernel/random/boot_id > "$STAGE/boot.id"
printf 'active\n' > "$STAGE/in-test"
printf '%s\n' "$GUI_PARENT" > "$STAGE/gui-parent.pid"
trap 'STATUS=$?; trap - EXIT; if ! sh "$STAGE/vrx.sh" stop "$STAGE"; then exit 5; fi; exit "$STATUS"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
nohup sh -c 'sleep "$2"; if test -f "$1/in-test"; then sh "$1/vrx.sh" stop "$1"; fi' sh "$STAGE" "$FALLBACK" > "$STAGE/fallback.log" 2>&1 < /dev/null &
FALLBACK_PID=$!
printf 'STACK_TRIAL_BEGIN gnd=%s daemon=%s\n' "$GND" "$DAEMON"
# Stop the UI supervisor before its child so it cannot reopen the framebuffer.
kill -STOP "$GUI_PARENT"
kill -TERM "$GND" "$DAEMON" "$GUI" "$GUI_PARENT"
kill -CONT "$GUI_PARENT"
sleep 1
for PID in "$GND" "$DAEMON" "$GUI" "$GUI_PARENT"; do
    if kill -0 "$PID" 2>/dev/null; then kill -KILL "$PID"; fi
done
for WAIT in 1 2 3 4 5 6; do
    if ! pidof ar_ldy_gnd >/dev/null && ! pidof daemon_sdio >/dev/null && ! pidof GlassesUI >/dev/null; then break; fi
    sleep 0.5
done
if pidof ar_ldy_gnd >/dev/null || pidof daemon_sdio >/dev/null || pidof GlassesUI >/dev/null; then
    printf 'STACK_STOP_FAILED\n'; exit 1
fi
# The original backend already calls SYS_Exit/VB_Exit during startup. Release
# the old UI's framebuffer allocations before those stock cleanup calls.
if test -d /sys/module/ar_framebuffer; then rmmod ar_framebuffer; fi
# Reset stale media-driver allocation state after the owning processes exit.
# These are the original modules and the original /etc/init.d/start.sh option.
rmmod ar_sys
rmmod ar_vb
insmod /mod/ar_vb.ko
insmod /mod/ar_sys.ko mpp_service=lib
printf 'STACK_MEDIA_DRIVERS_RELOADED\n'
rmmod artosyn_sdio
printf '%s' "$STAGE" > /sys/module/firmware_class/parameters/path
printf 0 > /sys/class/gpio/gpio3/value
sleep 0.2
printf 1 > /sys/class/gpio/gpio3/value
sleep 0.7
insmod /usrdata/fpv/boot_ar8030/artosyn_sdio.ko fw_name=candidate.img cfg_name=bb_config_gnd.json
sleep 3
cat /sys/bus/sdio/devices/*/uevent
if ! cat /sys/bus/sdio/devices/*/uevent | busybox grep -q '^SDIO_ID=4152:8031$'; then
    printf 'STACK_NATIVE_ENUMERATION_FAILED\n'; exit 1
fi
printf '%s\n' "$MODE" > "$STAGE/startup.mode"
printf 'STACK_AUTONOMOUS_BOOT no_host_rf_initialization\n'
# Any bootstrap helper has closed its SDIO descriptor and opened no sockets.
# These are the verified original executables and stock board arguments.
cd /usrdata/fpv
nohup ./daemon_sdio -i 1 > "$STAGE/native-daemon.log" 2>&1 < /dev/null &
printf '%s\n' "$!" > "$STAGE/native-daemon.pid"
sleep 0.5
cd /
nohup /usrdata/fpv/ar_ldy_gnd --board_type 485 --board_ver 16 --rf_board 16 > "$STAGE/native-backend.log" 2>&1 < /dev/null &
printf '%s\n' "$!" > "$STAGE/native-backend.pid"
sleep 1.5
cd /usrdata/fpv
nohup sh ./gui_run.sh 485 16 16 > "$STAGE/native-gui.log" 2>&1 < /dev/null &
printf '%s\n' "$!" > "$STAGE/gui-parent.pid"
printf 'STACK_NATIVE_WINDOW_BEGIN\n'
cat /proc/uptime
WAITED=0
while test "$WAITED" -lt "$WINDOW"; do
    if test -f "$STAGE/keep-running" || test -f "$STAGE/restored"; then break; fi
    sleep 1
    WAITED=$((WAITED + 1))
done
printf 'STACK_NATIVE_WINDOW_END\n'
cat /proc/uptime
pidof ar_ldy_gnd daemon_sdio
cat "$STAGE/native-daemon.log"
cat "$STAGE/native-backend.log"
cat "$STAGE/native-gui.log"

# Retain the requested RAM session after the host confirms native identity/link.
# This marker does not claim camera-video verification. Without it, restore stock.
if test -f "$STAGE/keep-running" && test -f "$STAGE/in-test" &&
   test ! -e "$STAGE/restored" && test ! -e "$STAGE/restoring"; then
    printf 'native-linked-running\n' > "$STAGE/native-left-running"
    rm -f "$STAGE/in-test"
    kill "$FALLBACK_PID" 2>/dev/null || true
    trap - EXIT HUP INT TERM
    printf 'STACK_NATIVE_LEFT_RUNNING_IN_RAM\n'
fi

}

stop() {
test ! -f "$STAGE/restored" || exit 0
test -f "$STAGE/stock.sha256" && test -f "$STAGE/boot.id" || exit 2
test "$(cat "$STAGE/boot.id")" = "$(cat /proc/sys/kernel/random/boot_id)" || exit 2
if ! mkdir "$STAGE/restoring" 2>/dev/null; then
    printf 'RESTORE_ALREADY_RUNNING\n'; exit 1
fi
trap 'rmdir "$STAGE/restoring" 2>/dev/null || true' EXIT
exec >> "$STAGE/restore.log" 2>&1
printf 'RESTORE_BEGIN\n'
if test -r "$STAGE/gui-parent.pid"; then
    read GUI_PARENT < "$STAGE/gui-parent.pid"
    if test "$(readlink /proc/$GUI_PARENT/exe 2>/dev/null)" = /bin/busybox &&
       tr '\000' ' ' < /proc/$GUI_PARENT/cmdline | busybox grep -q 'gui_run.sh 485 16 16'; then
        kill -STOP "$GUI_PARENT" 2>/dev/null || true
        kill -TERM "$GUI_PARENT" 2>/dev/null || true
        kill -CONT "$GUI_PARENT" 2>/dev/null || true
    fi
fi
for NAME in ar_ldy_gnd daemon_sdio GlassesUI; do
    EXPECTED=/usrdata/fpv/$NAME
    test "$NAME" != GlassesUI || EXPECTED=/tmp/fpvdata/bin/GlassesUI
    for PID in $(pidof "$NAME"); do
        if test "$(readlink /proc/$PID/exe 2>/dev/null)" = "$EXPECTED"; then
            kill -TERM "$PID" 2>/dev/null || true
        fi
    done
done
sleep 0.5
for NAME in ar_ldy_gnd daemon_sdio GlassesUI; do
    EXPECTED=/usrdata/fpv/$NAME
    test "$NAME" != GlassesUI || EXPECTED=/tmp/fpvdata/bin/GlassesUI
    for PID in $(pidof "$NAME"); do
        if test "$(readlink /proc/$PID/exe 2>/dev/null)" = "$EXPECTED"; then
            kill -KILL "$PID" 2>/dev/null || true
        fi
    done
done
for WAIT in 1 2 3 4 5 6; do
    if ! pidof ar_ldy_gnd >/dev/null && ! pidof daemon_sdio >/dev/null && ! pidof GlassesUI >/dev/null; then break; fi
    sleep 0.5
done
if pidof ar_ldy_gnd >/dev/null || pidof daemon_sdio >/dev/null || pidof GlassesUI >/dev/null; then
    printf 'RESTORE_STACK_STOP_FAILED\n'; exit 1
fi
if test -d /sys/module/ar_framebuffer; then
    if ! rmmod ar_framebuffer; then printf 'RESTORE_FRAMEBUFFER_UNLOAD_FAILED\n'; exit 1; fi
fi
if test -d /sys/module/ar_sys; then
    if ! rmmod ar_sys; then printf 'RESTORE_MEDIA_SYS_UNLOAD_FAILED\n'; exit 1; fi
fi
if test -d /sys/module/ar_vb; then
    if ! rmmod ar_vb; then printf 'RESTORE_MEDIA_VB_UNLOAD_FAILED\n'; exit 1; fi
fi
if ! insmod /mod/ar_vb.ko || ! insmod /mod/ar_sys.ko mpp_service=lib; then
    printf 'RESTORE_MEDIA_LOAD_FAILED\n'; exit 1
fi
printf /usrdata/fpv/boot_ar8030 > /sys/module/firmware_class/parameters/path
if test -d /sys/module/artosyn_sdio; then
    if ! rmmod artosyn_sdio; then printf 'RESTORE_UNLOAD_FAILED\n'; exit 1; fi
fi
printf 0 > /sys/class/gpio/gpio3/value
sleep 0.2
printf 1 > /sys/class/gpio/gpio3/value
sleep 0.7
if ! insmod /usrdata/fpv/boot_ar8030/artosyn_sdio.ko fw_name=bb_demo_gnd_3v3.img cfg_name=bb_config_gnd.json; then
    printf 'RESTORE_LOAD_FAILED\n'; exit 1
fi
sleep 3
cd /usrdata/fpv
nohup ./daemon_sdio -i 1 > "$STAGE/stock-daemon.log" 2>&1 < /dev/null &
cd /
nohup /usrdata/fpv/ar_ldy_gnd --board_type 485 --board_ver 16 --rf_board 16 > "$STAGE/stock-backend.log" 2>&1 < /dev/null &
sleep 1.5
cd /usrdata/fpv
nohup sh ./gui_run.sh 485 16 16 > "$STAGE/stock-gui.log" 2>&1 < /dev/null &
cat /sys/bus/sdio/devices/*/uevent
sha256sum /usrdata/fpv/boot_ar8030/bb_demo_gnd_3v3.img
if ! sha256sum -c "$STAGE/stock.sha256" ||
   ! cat /sys/bus/sdio/devices/*/uevent | busybox grep -q '^SDIO_ID=4152:8031$' ||
   ! pidof daemon_sdio >/dev/null || ! pidof ar_ldy_gnd >/dev/null; then
    printf 'RESTORE_VERIFICATION_FAILED\n'; exit 1
fi
test "$(cat "$STAGE/boot.id")" = "$(cat /proc/sys/kernel/random/boot_id)" || exit 1
printf 'RESTORE_COMPLETE\n'
printf 'done\n' > "$STAGE/restored"
rm -f "$STAGE/in-test"
# The host must still perform the documented full reboot/getter/video checks.

}

retain() {
    test -f "$STAGE/in-test"
    test ! -e "$STAGE/restored"
    test ! -e "$STAGE/restoring"
    printf native-link-confirmed > "$STAGE/keep-running"
    for WAIT in 1 2 3 4 5 6 7 8 9 10; do
        if test -f "$STAGE/native-left-running"; then
            printf 'Open8030 is running in RAM.\n'
            return 0
        fi
        sleep 1
    done
    printf 'Acceptance did not complete; check the stage logs.\n' >&2
    return 1
}

case "$ACTION" in
    start) start ;;
    retain) retain ;;
    stop) set +e; stop ;;
    *) exit 2 ;;
esac
