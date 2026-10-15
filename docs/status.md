# Firmware status

Open8030 targets AR8030-based video receivers, with potential transmitter support.
Hardware validation currently covers only the Ascent VRX with matched 18.21.10
board inputs. Other AR8030 receiver variants and transmitter/AP operation remain
unverified; the supplied loading helpers target the tested Ascent VRX configuration.

## Working baseline

The baseline provides a source-built bootstrap, autonomous initialization,
normal loader JSON and OTP identity, configured channel search, native MAC/DEV
handling, login, MCS handling, selected status/events and copy-mode sockets.
Camera video passes through the original Linux daemon, receiver backend and UI.
Host stop/start and stop/deinit/init/start have hardware coverage, as does
candidate removal and readmission.

Flash identification and read-only factory-offset lookup are integrated into
boot. Only the zero-offset fallback has hardware coverage; valid factory-record
read parity remains unverified. Power calibration covers initial user-0 settings.
Caches and interrupts remain disabled for cooperative polling.

The container built without a stock image and the combined `run` command were
tested on 2026-10-14. Native firmware identity, loader configuration and RF link
were confirmed, and a visible camera scene was captured directly from the host's
HDMI capture device. `run` left Open8030 running without a separate keep command.
This does not establish RF frame rate, end-to-end latency or telemetry parity.
[The baseline manifest](../baseline.json) records exact input/output hashes and
memory usage; detailed captures and lab records remain private.

Earlier in that session, both stock and replacement firmware showed a black
camera area with live overlays. A visible scene returned after a full receiver
reboot into stock and remained visible after loading Open8030 again. The cause
of the earlier black picture has not been isolated.

## Remaining work

- Dynamic host power controls and broader channel, band and RF policy support.
- AP-time synchronization and complete telemetry/host-command parity.
- Wider modes, other hardware variants and transmitter support.
- Physical peer-silence/restart, pending-DMA recovery and extended operation tests.
- A complete interrupt/timer platform port and validated resource margins.
- Public board-profile provisioning, repeated cold starts, reliable recovery
  from black camera output and persistent installation/rollback.

RF behavior changes require fresh hardware video validation against this baseline.

## Video verification

Connect the VRX's HDMI output to an **HDMI-to-USB capture adapter**, then connect
the adapter to the Linux host. In the tested setup, the adapter appears as
`/dev/video0`; this is the captured HDMI output, not a direct USB camera connection
to the radio. Device numbering may differ on another host.

Install `v4l-utils` and `ffmpeg`, then record the capture device and available modes
(replace `/dev/video0` below if your adapter uses a different device):

```sh
mkdir -p .local
v4l2-ctl --device /dev/video0 --all > .local/video-device.txt
v4l2-ctl --device /dev/video0 --list-formats-ext
```

The accepted capture device supported MJPEG at 1920x1080/30 fps. If your device
advertises that mode, capture and inspect it directly:

```sh
ffmpeg -f v4l2 -input_format mjpeg -video_size 1920x1080 -framerate 30 \
  -i /dev/video0 -t 10 -c:v copy .local/open8030-video.mkv
ffplay .local/open8030-video.mkv
```

Otherwise select a mode reported by your capture device. Use a new output filename
for each capture. Verify replacement identity with `./open8030 status HOST` before
and after recording, and inspect fresh camera frames after starting the RAM
session. Capture cadence does not establish RF frame rate or end-to-end latency.
Keep captures and device identifiers in ignored `.local/`.
