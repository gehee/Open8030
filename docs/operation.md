# Operation and troubleshooting

Use the [README](../README.md#build-and-run-on-the-ascent-vrx) for the short build/run
workflow. This page explains session behavior and how to interpret failures.

## What `run` retains

`run` creates `/tmp/ar8030-zephyr-<session>` on the receiver, uploads the candidate
and control script, and copies the existing loader JSON there. The script checks
the accepted image, configuration, original executables/modules and SDIO identity
before changing the running stack. The tested backend arguments remain
`--board_type 485 --board_ver 16 --rf_board 16`; these are original hardware IDs,
not configurable product names.

The host waits up to 45 seconds for service startup and then up to 30 seconds for
replacement identity and RF link. Successful checks retain the RAM session and
disarm rollback. There is no five-minute limit after successful retention and no
separate `keep` command. Closing the host terminal after `run` succeeds does not
stop Open8030. `stop` or a receiver reboot restores stock operation.

Before retention, the device script uses a 300-second observation window and a
separate fallback scheduled for 355 seconds after it is armed. Failure paths may
restore sooner. These timers attempt recovery; they cannot guarantee recovery from
a hung kernel or a failed driver reload. A preflight failure can happen before
any services change or any rollback timer is armed.

## Binding a transmitter

With Open8030 running on the tested Ascent VRX, use the normal binding-button
procedure on the receiver and transmitter. The original Linux backend controls
the binding window and saves the selected peer. There is no additional Open8030
binding command to run on the host computer.

Wait for the stock UI to finish binding, then check the visible camera scene.
The receiver backend log records entry into pairing, success or failure, and
peer selection. A saved peer can reconnect after reboot, but a reboot restores
the stock radio firmware; run Open8030 again to load the replacement.

This path targets the tested DEV mode-0/slot-0 configuration. The protocol details
and validation boundaries are in [binding coordination](wireless-protocol.md#button-binding-and-host-coordination).

## Local outputs and receiver logs

| Location | Contents |
| --- | --- |
| `.local/open8030.img` | Current generated hardware image |
| `.local/open8030.manifest.json` | Packaging inputs, hashes and configuration layout |
| `.local/build-ar8030-bringup/` | Runtime build, ELF, binary and generated Zephyr configuration |
| `.local/build-ar8030-bootstrap/` | Bootstrap build and manifest |
| `.local/build-qemu-ready/` | QEMU build |
| `.local/vrx-<host-hash>.json` | Hostname and receiver stage path used by session controls |
| Receiver stage `run.log`, `exit.status` | Startup/retention output and launcher exit code |
| Receiver stage `native-daemon.log`, `native-backend.log`, `native-gui.log` | Original Linux services while Open8030 is loaded |
| Receiver stage `restore.log`, `stock-*.log`, `fallback.log` | Restoration and fallback output when those paths run |

Use the exact stage path printed by `run` when reading receiver logs over SSH.
The saved local session is keyed by the host string, so switching between a
hostname and IP address creates different records. Use the same checkout and host
argument for `run` and `stop`. Receiver `/tmp` logs are lost on reboot.

## Reading status

```sh
./open8030 status YOUR_VRX_IP
```

Success confirms an `ar8030-zephyr/` identity and RF link state 2. The literal
identity currently contains `rf=unsupported`; this is a fixed diagnostic string,
not a live link-state field. Read `link_state` for the link check.

`video_verified: false` means the command did not inspect camera output. It does
not mean that video has failed. The command errors on stock identity, an unlinked
replacement or an unavailable daemon; it is not a general stock-firmware monitor.

To verify video, use the VRX HDMI output through an HDMI-to-USB capture adapter
and inspect fresh camera frames, as described in [video verification](status.md#video-verification).
Live overlays, socket traffic and RF link alone are insufficient. Capture rate is
the adapter's output cadence, not a measurement of RF frame rate or latency.

## Common failures

| Symptom | Interpretation and next step |
| --- | --- |
| Missing or unmatched private profile | Hardware builds need the accepted private input. Native tests and QEMU remain available without it. |
| Generated container does not match the recorded layout | The compiled inputs or packaging differ from the baseline. Compare source/toolchain/profile hashes before accepting an image. |
| Startup exits before the observation window | Read `run.log` and `exit.status`; a software/configuration hash or process-layout check may have failed. |
| A session is already active | Inspect that saved session. Use `stop` after completed startup; a failed preflight may leave a stage record too, so inspect logs before treating it as a running radio. |
| RF link was not confirmed | Automatic rollback remains armed if startup reached that point. Inspect startup/service logs and confirm the matched transmitter/setup. |
| Camera area is black but overlays are live | Reboot into stock and establish a visible scene before another run. This occurred with both stock and replacement firmware; its cause has not been isolated. |
| `stop` cannot find or use the saved stage | Check the host argument and checkout. A receiver reboot removes temporary stages and already returns the system to stock. |
| SSH disappears after receiver reboot | Check the host's USB network interface/address and the receiver address; the USB network device can reconnect without its previous temporary host configuration. |
| Capture device is busy or absent | Identify the HDMI-to-USB adapter with `v4l2-ctl --list-devices`; check its device number and whether another application owns it. |

`stop` checks original firmware hashes, SDIO enumeration and service presence.
It does not establish a restored camera scene, so inspect stock video afterward.
Reboot is the fallback when service-level recovery is insufficient.

## What the tests establish

`./open8030 test` runs Python packaging/host-helper checks and native C tests of
protocol parsing, state machines, bounds, calibration, storage and simulated
peripheral contracts. It does not load hardware or perform RF experiments.

`./open8030 qemu` exercises the Zephyr UART build: hello, fragmented framing and
resynchronization, a maximum-size heartbeat, diagnostic identity and rejection
of an unsupported radio command. It does not emulate AR8030 RF, SDIO DMA or HDMI.

A hardware result should record the board/release, profile and image hashes,
native identity, loader configuration, capture-device identity/settings and
observation duration. Socket counters describe offered, accepted, delivered and
dropped bytes; zero drops at that queue do not prove zero over-air loss. Current
hardware coverage and unresolved behavior are maintained in [status.md](status.md).
