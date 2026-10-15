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
tested on 2026-10-14. The development baseline adds DEV-side AP-time synchronization
and host getter `0x0100000c`, validated on 2026-10-15 UTC. It matches 2,304
original-instruction emulator cases. A 55-second native HDMI capture and two
20-second captures after stop/start and stop/deinit/init/start show the camera
scene. Native identity brackets each capture; all 104 sampled status responses
are linked and the sampled socket counters show zero endpoint drops.

The clock offset survives stop/start. Deinit/init produces the original unavailable
sentinel before reception starts, then fresh AP messages restore synchronization.
Fresh stock comparison confirms the same advancing transmitter-clock timeline.
The candidate was retained in RAM after video inspection. This does not establish
RF frame rate, independently measured end-to-end latency or full telemetry parity.
[The baseline manifest](../baseline.json) records exact input/output hashes and
memory usage; detailed captures and lab records remain private.

Earlier in that session, both stock and replacement firmware showed a black
camera area with live overlays. A visible scene returned after a full receiver
reboot into stock and remained visible after loading Open8030 again. The same
warm-restoration symptom recurred before the AP-time candidate was loaded; a full
receiver reboot again restored stock video. The cause has not been isolated.

The preceding development image added original register-read request
`0x01000064`, with 360 original-instruction comparisons and stock/native wire
checks. Its 30-second direct HDMI capture shows the same dark textured camera
scene as fresh stock. All 33 native status samples are linked, with advancing AP
time and zero sampled socket drops. Scene lighting/freshness was not controlled;
this is not a new bright-scene quality or latency measurement. The image was
retained after inspection.

The preceding image fixed calibration lifetime across normal context rebuilds:
loaded high-band TX compensation survives stop/start and stop/deinit/init/start.
The previous code cleared it until another preparation notification. Validation
includes 128 original-reset cases, regression tests that fail on the previous
code, all 21 native C suites and 18 Python tests. Hardware register checks confirm
retention across both lifecycle sequences. Startup, post-transition and final
reload captures total 65 seconds with 77 linked samples, advancing AP time and
zero sampled socket drops; the dark textured scene matches freshly rebooted
stock. This is not controlled freshness or bright-scene quality verification.

The first test window expired before retention. A subsequent warm reload showed
the disconnected HDMI background despite a radio link; rebooting into stock and
reloading restored the scene. The final candidate was inspected and retained in
RAM. Its planned candidate-removal check stopped before mutation when the large
RX diagnostic returned an unexpected backend response. See [calibration lifetime](parity.md#calibration-lifetime-across-normal-restart)
for the original-comparison assumptions and remaining preparation gaps.

The previous image added bounded RX diagnostic reads through the original daemon,
whose encoder cannot forward the former complete record. Ten complete records
read in ranges and eleven invalid requests pass on hardware. Candidate removal
and readmission now also confirm calibration retention: the receiver reconnects
without a recovery failure and the compensation words remain unchanged.
The 15-second startup capture and 20-second post-recovery capture show the same
dark textured camera scene as freshly rebooted stock. All 41 samples remain
linked with valid advancing AP time and zero sampled socket drops. The image
was retained in RAM after inspection. These checks do not establish physical
peer-loss recovery, controlled freshness or full protocol parity.

The preceding image installed the normal DEV frame through a source-built command
builder, with retained metadata updated during retuning and profile changes.
It matches 568 original construction cases and 6,720 updates. All 22 native C
suites and 18 Python tests pass. Live command registers match stock, and 50 seconds
of startup/restart/reinit HDMI captures retain the dark stock-matching camera
scene, with 59 linked/AP-time samples and zero sampled socket drops. The image
was retained in RAM after inspection. The remaining preparation IRQ actions and
broader frame modes are still open; see [command construction](parity.md#command-construction-and-retained-metadata).

The preceding image connected that metadata to preparation IRQ handling: cursor
selection, cached board-band writes, calibration reuse and TX bandwidth
look-ahead. The helper matches 14,864 original-instruction cases, including
15,956 ordered write arguments. All 22 native C suites and 18 Python tests pass.
Startup, stop/start, deinit/init/start and candidate removal/readmission retain
compensation and video. The four direct HDMI-to-USB captures total 70 seconds,
with 82 linked/AP-time samples and zero sampled socket drops; the dark camera
scene matches fresh stock. The image was retained in RAM after inspection.
Scan callbacks, PA-sleep policy and retained-buffer ownership integration remain
open, along with other runtime bands/modes and low-power acquisition. See
[preparation integration](parity.md#command-preparation-integration).

The preceding image adds an independent configured-channel measurement sweep,
keeping link RX/TX PLLs fixed. It matches 7,808 scheduler cases and 66 measurement
tuning cases against original instructions. All 23 native C suites and 18 Python
tests pass. Startup, restart, reinit and candidate recovery preserve compensation
and video: 70 seconds of inspected HDMI capture, 82 linked/AP-time samples and
zero sampled socket drops. The candidate was retained in RAM.

This is incomplete scan behavior. Stock sampling observes four measurement
frequencies while native sampling observes 33 from its configured 42-channel
list. Native polling also excludes many ambiguous power events. Active-catalog
policy and IRQ timing remain explicit gaps. Scoring was added subsequently; see
[independent measurement scan](parity.md#independent-measurement-scan).

A subsequent comparison confirms why the scan catalogs differ: the original
Linux backend needs channel-information getter `0x0100000a` to populate its
catalog before sending work-list setter `0x02000023`. Open8030 rejects the getter,
leaving the backend catalog empty and suppressing the setter. Stock reports four
enabled channels out of 42; the native backend desires the same four frequencies
but cannot resolve their indices. Both commands remain unimplemented. See
[the catalog dependency](parity.md#active-catalog-and-linux-backend-dependency).
No firmware bytes changed during this investigation. A fresh 20-second native
HDMI capture preserves the stock-matching dark scene, with 23 linked/AP-time
samples and zero sampled socket drops; the accepted image remains running in RAM.

A source-only catalog module now matches the original serialization and work-list
decisions in 1,645 instruction-emulator cases. Its policy, AP tuning and scan-reset
callbacks are comparison boundaries, and processed scores are supplied inputs.
It is not yet part of the firmware build or host dispatcher; the three catalog
commands remain unsupported on the device. Its sanitizer test also checks the
existing scan iterator against the selected list. A fresh 20-second direct HDMI
capture of the unchanged accepted image passes with 23 linked/AP-time samples
and zero sampled socket drops. See [catalog contracts](parity.md#channel-catalog-command-contracts)
for the exact scope and remaining integration work.

The current image feeds confirmed measurement events into original base-channel
averaging and integer score conversion. The helper matches 1,426 conversion
inputs and 3,726 processing cases, including ranking and startup decisions.
Runtime automatic selection and group policy remain unintegrated. The version-2
scan diagnostic exposes processed scores separately from sample counts. Startup
hardware observations cover all 42 configured channels and reach readiness;
stop/start and deinit/init/start reset the native histories before collection
resumes. Original lifecycle retention still needs comparison.

All 25 native C suites and 18 Python tests pass. Startup, restart, reinit and
candidate recovery retain compensation and video: 90 seconds of inspected direct
HDMI capture, 102 linked/AP-time samples and zero sampled socket drops. The dark
camera scene matches fresh stock, and the candidate remains running in RAM.
The work-list commands, differing active catalog and excluded/ambiguous polling
events still prevent full scan parity. See [score processing](parity.md#base-channel-score-processing).

A separate configuration-only test reproduced acquisition failure at initial
power level 11 using the accepted AP-time image. No video was delivered;
restoring the original level-25 configuration restored the stream. This failure
does not depend on the later, unaccepted dynamic-power code. Low-power operation
remains unresolved; see [configuration](configuration.md#power-and-factory-data).

## Remaining work

[Original firmware parity](parity.md) tracks the comparison scope, known host
request and PHY-command gaps, and the evidence required to close each area.
Full original-firmware coverage has not been established.

- Dynamic host power controls and broader channel, band and RF policy support.
- Complete telemetry/host-command parity and broader time-synchronization modes.
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
