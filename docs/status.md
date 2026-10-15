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

The preceding image feeds confirmed measurement events into original base-channel
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

The work-list dependencies now have source-only helpers for backup-channel
ranking, queued channel requests and auxiliary-scan reset. They match 2,532
original-instruction cases, including queue failures and reset write ordering.
The dedicated native suite passes with address/undefined-behavior sanitizers.
These helpers are not included in the runtime build: channel-policy dispatch,
queue consumption, hopping messages and the complete backend workflow remain
open. A fresh 20-second HDMI-to-USB capture of the unchanged retained image shows
the same dark textured scene, with 23 linked/AP-time samples and zero sampled
socket drops. This is a baseline video recheck, not hardware validation of the
new helpers. See [work-list helper contracts](parity.md#work-list-helper-contracts).

Selector-1 channel policy now also has a source-only implementation matching
3,440 original-instruction cases. The comparison includes manual requests,
entering/leaving hopping, backup-channel messages, nested lock order, timer
arming and queue/PHY failures. Its dedicated sanitizer suite and the catalog/
channel-work dependency suites pass. The runtime still needs the queued
channel-change consumer and protocol completion/rollback behavior before these
helpers can support the backend workflow. See
[channel policy](parity.md#selector-1-channel-policy).

The next source-only helpers cover queued channel requests and their
completion/rollback handler. They match 4,330 original request cases and 9,312
completion cases, including DEV/AP encoding, enqueue failures, full-channel
matching and counter resets. Transaction, channel-work and policy sanitizer
suites pass. A fresh 20-second HDMI-to-USB capture rechecks the unchanged
baseline: the same dark camera scene, 23 linked/AP-time samples and zero sampled
socket drops. PHY command callbacks, tuning/monitor behavior and normal runtime
integration remain open; the catalog commands are still unsupported. See
[channel transactions](parity.md#channel-request-and-completion-contracts).

The source-only PHY command-4 callback now matches 5,728 original cases across
AP/DEV modes 0–3, including malformed requests, channel/state updates, ordered
tuning and notifications, auxiliary search, completion suppression and cross-band
monitor preparation. Its sanitizer suite passes. Actual tuning, PHY command
transport and monitor-driven completion are still unintegrated. A fresh
20-second capture of the unchanged baseline through the HDMI-to-USB adapter
again shows the reference dark scene, with 23 linked/AP-time samples and zero
sampled socket drops. See [channel-change callbacks](parity.md#phy-channel-change-callback).

The current image uses the compared generic tuner for measurement RX user 11.
Channel resolution and tuning match 2,328 and 11,184 original-instruction cases
respectively. Other users, TX power callbacks, subchannels and channel-policy
integration remain open. All 30 native C suites and 18 Python tests pass; the
standard build reproduces the hardware-tested image. Startup, restart, reinit
and candidate readmission preserve compensation and camera video: 90 seconds
of inspected HDMI-to-USB capture, 102 linked/AP-time samples and zero sampled
socket drops. The image remains running in RAM.

The initial fresh-stock capture showed the same dark textured camera scene.
After the first native window expired, a second receiver reboot produced a stock
fallback background despite a reported link. Native loading restored the scene,
and the complete validation sequence was repeated. Startup reliability remains
unresolved; these captures do not establish controlled freshness or latency.
The active-catalog mismatch and ambiguous polling events also remain. See
[channel resolution and tuning](parity.md#channel-resolution-and-generic-tuning).

The initial source-only monitor comparison matched 5,784 original observation cases and
7,200 periodic cases. It covers channel-change success/timeout posts, shared
counters, band-hold release, AP/DEV reset thresholds, loss-callback cleanup and
automatic-hop decisions. Its dedicated sanitizer suite passed. At that point it was not
connected to runtime; the later integration is described below. A fresh
20-second HDMI-to-USB capture of the unchanged accepted image shows the reference
dark camera scene, with 23 linked/AP-time samples and zero sampled socket drops.
The accepted image remains in RAM. See
[monitor completion and its boundaries](parity.md#channel-change-monitor-completion-and-hopping).

The initial source-only PHY transport helper matched 17,912 original-instruction cases
covering enqueue, transmit countdown, cancellation, AP time generation and shared
receive-timer dispatch. Its sanitizer suite passes. Callback bodies and runtime
integration remain open. A fresh
20-second HDMI-to-USB capture rechecks the unchanged accepted image, with the
reference dark scene, 23 linked/AP-time samples and zero sampled socket drops.
See [pending PHY command transport](parity.md#pending-phy-command-transport).

The initial source-only receive helper matched 26,120 original-instruction cases for
validation, cache replacement, mode-3 flags, command-0 admission records and
timer decisions. Another 6,455 receive-then-expire sequences confirm that cached
command 0 or 7 does not dispatch through the original callback table. Both PHY
sanitizer suites and the prior 17,912 transport comparisons pass. This does not
close runtime command effects or physical timer/IRQ integration. A fresh
20-second HDMI-to-USB capture of the unchanged accepted image shows the reference
dark scene, 23 linked/AP-time samples and zero sampled socket drops; that image
remains running. See [receive coverage](parity.md#phy-control-receive-and-cache-replacement).

The latest runtime now uses the compared receive/cache and expiry helpers for
normal mode-0 operation. Command 9 follows the original descriptor's absent
callback, and command-0 cache replacement safely handles declared lengths above
four. The prior receive/expiry traces still match; RF integration and both PHY
sanitizer suites pass, along with 18 packaging/tool tests. Broader command effects,
PHY sender/channel-policy integration and mode-3 admission remain open. See
[runtime receive integration](parity.md#runtime-phy-receive-integration).

Hardware startup, stop/start and stop/deinit/init/start captures total 60 seconds,
with 69 linked/AP-time samples and zero sampled socket drops. The dark textured
camera scene matches a fresh 10-second stock HDMI-to-USB reference. Calibration
registers remain stable across both lifecycle sequences, and deinit/init clears
AP-time validity before fresh reception restores it. The new image is retained
in RAM. This does not establish controlled freshness, latency, mode-3 operation
or hardware execution of every command/cache boundary.

Normal mode-0 MCS sender requests now use the shared PHY transport for enqueue,
interleaver transmission and cancellation. The MCS policy reads a snapshot of
the authoritative pending slot before each decision. All 17,912 original transport
trace comparisons still pass, as do RF integration, MCS policy and transport
sanitizer suites. Channel-policy producers and broader outgoing callbacks remain
open. See [shared PHY sender](parity.md#shared-phy-sender-integration).

Hardware startup, stop/start and stop/deinit/init/start captures total 60 seconds,
with 69 linked/AP-time samples and zero sampled socket drops. All inspected
frames show the dark textured scene from a fresh stock HDMI-to-USB reference.
Each candidate session records six MCS requests/acknowledgements, five
advertisements/cancellations and 25 control-register writes, with no reset
errors. Calibration survives both lifecycle sequences. The image is retained in
RAM; controlled freshness, latency and multi-producer behavior remain unverified.

The normal mode-0 loss/recovery path now uses the compared channel monitor for
logical RX8. Its flags and counters also supply recovery diagnostics. Reset and
loss callbacks retain bounded waits, DMA ownership protection and deferred host
recovery. All 12,984 original monitor trace comparisons and three sanitizer suites
pass. Hopping and channel-completion workers remain unintegrated; see
[runtime monitor coverage](parity.md#runtime-channel-monitor-integration).

Hardware startup, stop/start, stop/deinit/init/start and candidate
removal/readmission captures total 80 seconds, with 92 linked/AP-time samples
and zero sampled socket drops. Inspected frames match the dark textured camera
scene in a fresh 10-second stock reference, captured directly from `/dev/video0`
through the HDMI-to-USB adapter. Calibration remains stable through each
transition; recovery diagnostics show an enabled, locked monitor with positive
threshold and interval. Each session exercises MCS advertisements, cancellation
and control-register writes without reset errors. The image is retained in RAM.
Candidate removal is host-driven recovery; it does not establish physical
peer-silence parity, controlled video freshness or latency.

Power application and band-offset helpers now match 63,060 fresh original
instruction cases, including per-write cache state and effect order. The power
cache has an original alias: applying another user also updates user 0's cached
level, without implying a second hardware power change. Both sanitizer suites
pass. That initial comparison kept the helpers outside the runtime; channel-tuning callbacks,
dynamic host policy and physical power validation remain open. See
[tuning power callbacks](parity.md#tuning-power-callbacks).

That source-only rebuild matches the retained stage-187 image byte-for-byte. A fresh
30-second HDMI-to-USB capture preserves the camera scene, with 33 linked/AP-time
samples and zero sampled socket drops. The new helpers have no hardware
validation yet; no new stock capture or lifecycle test was performed.

Initial loader user-0 power now uses the compared helper. Resolved calibration
is copied into the RF runtime and saved host configuration, validated against
the initial gain plan, and restored through lifecycle/recovery preparation.
Bounded diagnostic plans clear the current calibration context. Four sanitizer
suites pass, including calibration ownership, rejection without RF writes and
retention checks; all 63,060 original power trace comparisons still match.
Dynamic tuning, host setters and other users' initialization remain open. See
[runtime power integration](parity.md#runtime-calibration-and-initial-power-application).

A connected-state power difference from stock remains: the backend requests
user-0 level 14 through an unsupported setter, while Open8030 keeps its configured
level-25 gain fields. Earlier accepted-image register evidence has the same
native values. The new integration preserves that baseline; it does not close
host power-policy parity.

Hardware startup, stop/start, stop/deinit/init/start and candidate
removal/readmission captures total 80 seconds, with 92 linked/AP-time samples
and zero sampled socket drops. Inspected frames preserve the dark textured
camera scene and OSD from a fresh 10-second stock reference captured directly
through `/dev/video0` and the HDMI-to-USB adapter. Calibration compensation and
native user-0 gain fields remain stable through all transitions; each session
records six MCS requests/acknowledgements, five advertisements/cancellations and
25 control-register writes without reset errors. The candidate is retained in
RAM. Host-driven candidate recovery does not establish physical peer-loss
parity; controlled video freshness, latency and RF power remain unmeasured.

A separate configuration-only test reproduced acquisition failure at initial
power level 11 using the accepted AP-time image. No video was delivered;
restoring the original level-25 configuration restored the stream. This failure
does not depend on the later, unaccepted dynamic-power code. Low-power operation
remains unresolved; see [configuration](configuration.md#power-and-factory-data).

Stage 190 adds source-only power-command helpers backed by 16,646 original
instruction comparison scenarios and fresh stock wire/register checks. Runtime
integration failed acquisition after the backend selected level 11; the
30-second capture showed the disconnected UI. That candidate was rejected and
its runtime changes removed. Accepted stage 189 was restored after a receiver
reboot, with 30 seconds of directly inspected HDMI-to-USB camera video, 33 linked
samples, advancing AP time and zero sampled socket drops. It remains running in
RAM, and the standard build reproduces its exact image. The power commands remain
unsupported at runtime; see [the comparison and failed trial](parity.md#manual-power-commands-compared-helpers-failed-integration).

Stage 191 adds a source-only interleaver service with 9,760 original-instruction
comparison cases, including drain/retry sequencing, clock-correction boundaries
and callback mutation. It does not yet execute in the runtime or fix low-power
acquisition. The accepted stage-189 image remains byte-identical and running;
a fresh 30-second HDMI-to-USB capture shows the camera scene with 33 linked
samples and zero sampled socket drops. See [interleaver coverage](parity.md#interleaver-service-and-low-power-investigation)
for intercepted callbacks and intentional validation differences.

Stage 192 adds source-only TX-MCS producers and application/reset callbacks, with
23,670 nonfatal original scenarios, 846 explicitly different assertion outcomes,
and 324 composed request/interleaver sequences. It fixes the source-only
retry-reset interface to pass the affected user. Two sanitizer suites and 18
Python tests pass. Accepted stage 189 still builds identically and remains
running; a fresh 30-second capture shows camera video with 33 linked samples.
Runtime ownership and TX-attempt history remained integration dependencies;
low-power acquisition is unresolved. See [TX MCS coverage](parity.md#tx-mcs-producer-and-application-callbacks).

Stage 193 compares source-only pre-encode and TX-end bookkeeping in 5,586
original-instruction scenarios. History records attempts during pre-encode;
it does not establish completed transmissions or peer reception. TX-end posts
a separate release request for the remembered user. Three sanitizer suites and
18 Python tests pass. Accepted stage 189 still builds identically and remains
running; a fresh 30-second HDMI-to-USB capture shows the camera scene with 33
linked samples and zero sampled socket drops. Queue delivery, packet ownership
and integration of the compared TX helpers remain open. See
[TX interrupt coverage](parity.md#tx-pre-encode-history-and-end-notification).

Stage 194 compares retained-descriptor retirement in 2,588 original-instruction
scenarios, including 9,857 release calls. It separately confirms the current
copy-ring bookkeeping in 440 valid cases. Original pre-encode retention comes
from context bits 28..31; the current runtime uses one. The new descriptor helper
is source-only, and the underlying stream-release consumer remains unverified.
Three sanitizer suites and 18 Python tests pass. The standard build remains
identical to accepted stage 189; a fresh 30-second HDMI-to-USB capture shows the
camera scene with 33 linked samples and zero sampled socket drops. See
[retirement coverage](parity.md#retained-descriptor-retirement).

Stage 195 compares the stream-release consumer, lookup/reference updates, entry
traversal and queue consumption in 3,600 original scenarios, including 96 composed
retirement sequences. It preserves partial-consumption and callback ordering,
requested-byte accounting wrap, and the original two reference drops. The helper
remains source-only; producer ownership and RTOS integration are still open.
Three sanitizer suites and 18 Python tests pass. The standard build reproduces
accepted stage 189; a fresh 30-second HDMI-to-USB capture shows the camera scene
with 33 linked samples and zero sampled socket drops. See
[stream-release coverage](parity.md#stream-release-consumer-and-queue-consumption).

Stage 196 corrects the source-only TX bookkeeping range to users 0..9. Fresh
execution shows the original initializer visits ten records; forcing users 10/11
overwrites adjacent callback-list memory. The native helpers now reject these
indices. Replay passes 30,454 earlier scenarios within the corrected scope,
including 534 deliberately different assertion outcomes; 9,408 unsafe probes
are excluded from supported-user coverage. Three sanitizer suites and 18 Python
tests pass. The device is unaffected: the standard build reproduces accepted
stage 189, and a fresh 30-second HDMI-to-USB capture shows the camera scene with
33 linked samples and zero sampled socket drops. See
[the bounds correction](parity.md#corrected-tx-bookkeeping-bounds).

Stage 197 compares TX stream acquisition in 3,356 original scenarios, including
128 acquire/release sequences. Nonempty acquisition holds the reference later
consumed by release. Both retained-entry and byte-ring paths cover span budgets,
wrap, empty results and timeout behavior. The earlier 3,600 release scenarios
also pass regression replay. Three sanitizer suites and 18 Python tests pass.
The helper remains source-only: allocation, teardown, concurrency and runtime
integration are still open. The build reproduces accepted stage 189; a fresh
30-second HDMI-to-USB capture shows the camera scene with 33 linked samples and
zero sampled socket drops. See [acquisition coverage](parity.md#stream-acquisition-and-reference-ownership).

Stage 198 compares the prepared TX packet phase in 492 original scenarios,
including 18 sequences through original descriptor retirement and stream release.
There are 448 successful outcomes and 44 original assertion paths matched up to
their failure boundary. A short acquisition can leave submitted segments and
held references; it is not a clean rollback. The new helper remains source-only,
with full-handler integration and recovery still open. Four sanitizer suites,
18 Python tests and 3,356 acquisition regression scenarios pass. The build still
reproduces stage 189; a fresh 30-second HDMI-to-USB capture shows the camera scene
with 33 linked samples and zero sampled socket drops. See
[packet preparation coverage](parity.md#prepared-tx-packet-assembly-and-retirement).

Stage 199 extends the comparison through TX slot reservation and post-submission
accounting: 537 original scenarios, including 27 reserve/prepare/account/retire
sequences. It checks ring wrap, full/null-slot failures, selected stream-counter
resets, and timestamped statistics with truncated fields and wrapping totals.
These are accounting effects, not host notifications. Four sanitizer suites and
18 Python tests pass. The helpers remain source-only; the build reproduces stage
189. A fresh 30-second HDMI-to-USB capture shows the camera scene with 33 linked
samples and zero sampled socket drops. See
[reservation and accounting coverage](parity.md#tx-slot-reservation-and-post-submission-accounting).

Stage 200 integrates normal-key user-0 TX MCS requests and the compared
interleaver drain into the running firmware. Synthetic RF tests verify six drain
events followed by application on the seventh, pending-target preservation,
other-user isolation and event order. Replaying the original traces passes
7,540 interleaver cases and 18,030 TX MCS cases, including 534 assertion prefixes.
Three sanitizer suites and 18 Python tests pass.

The new image passes startup, stop/start, deinit/init/start and candidate
removal/readmission. Its 80 seconds of inspected HDMI-to-USB capture show the
same camera scene as fresh stock, with 92 linked samples and zero sampled socket
drops. Each session exercised an increase from TX MCS 1 to 2; **no hardware
decrease was observed**. The image remains running in RAM. Hardware decrease
testing, full retained-buffer ownership, retries/history and broader keys remain
open. See [runtime TX MCS coverage](parity.md#runtime-tx-mcs-drain-integration).

Stage 201 integrates user-0 pre-encode attempt history. Five sanitizer suites,
18 Python tests, 4,884 IRQ trace replays and 270 fresh composed original
request/interleaver/pre-encode/TX-end sequences pass. Hardware now exercises the
decrease path: startup, restart and candidate readmission record one, one and
three completed decreases respectively, with corresponding history updates.
Restart/readmission samples observe MCS 2 → 1 → 2 and matching register changes.
History and counters reset when the radio context is rebuilt.

Startup, post-drain, restart, reinitialization and readmission captures total
110 seconds and 125 linked samples with zero sampled socket drops. Inspected
frames retain the stock-matching camera scene. The candidate remains running in
RAM. These observations do not establish exact interrupt timing, peer reception,
sustained adaptation under controlled loss or full retained-buffer ownership.
See [attempt-history coverage](parity.md#runtime-tx-attempt-history).

Stage 202 compares the queue producer against 1,554 original scenarios, including
18 write/acquire/release sequences. It covers partial publication, ring wrap,
overwrite of unacquired bytes, space rejection, disable during a write, timeout
and counter behavior. Four sanitizer suites and 18 Python tests pass. This is
source-only ownership work; the standard build still reproduces stage 201.
A fresh 30-second HDMI-to-USB capture shows the camera scene, with 33 linked
samples and zero sampled socket drops. Stage 201 remains running in RAM.
See [queue producer coverage](parity.md#tx-queue-producer-and-backpressure).

Stage 203 compares stream close and queue disable against 1,064 original
scenarios, including 24 acquire/close/retire sequences. Held references prevent
freeing until retirement; close disables busy queues and yields while waiting.
Five sanitizer suites, 18 Python tests and 1,554 producer regression cases pass.
The helpers remain source-only; the build reproduces stage 201. A fresh
30-second HDMI-to-USB capture shows the camera scene with 33 linked samples and
zero sampled socket drops. Stage 201 remains running in RAM. See
[stream close coverage](parity.md#stream-close-and-held-reference-lifetime).

Stage 204 compares handle lookup and close against 635 original scenarios,
including 24 acquisition/handle-close/retirement sequences. The handle remains
active during stream close and is cleared by a second table lookup after success;
failure of that lookup does not restore destroyed streams. Six sanitizer suites
and 18 Python tests pass. This remains source-only work; the build reproduces
stage 201. A fresh 30-second HDMI-to-USB capture shows the camera scene with
33 linked samples and zero sampled socket drops. Stage 201 remains running in
RAM. See [handle lifetime coverage](parity.md#handle-lookup-and-close-sequencing).

Stage 205 compares host close requests, TX/RX worker shutdown and exit
notifications across 575 original scenarios, including 16 composed close
sequences. TX announces exit before draining messages; the final reply follows
notification that the selected directions have exited. Four sanitizer suites and 18 Python tests pass.
The model remains source-only; the build reproduces stage 201. A fresh
30-second HDMI-to-USB capture shows the camera scene with 33 linked samples
and zero sampled socket drops. Stage 201 remains running in RAM. See
[socket shutdown coverage](parity.md#socket-worker-shutdown-and-close-replies).

Stage 206 integrates cooperative copy-mode socket close: finish or cancel the
active write, discard queued work, and retain the close reply until SDIO accepts
it. Metadata-only endpoint reset removes the large buffer clears that caused
video drops during rapid open/close testing. The final image passes 24 hardware
close/reopen cycles, including four active-write cancellations, with zero socket
drops. Completion replies match 180 original-instruction cases; nine sanitizer
suites and 18 Python tests pass. Six HDMI-to-USB captures total 120 seconds across
startup, close stress, restart, reinitialization and recovery. All 138 samples are
linked with advancing AP time and zero sampled socket drops; inspected camera
frames match the fresh stock scene. Stage 206 is the retained RAM baseline.
Original spare-port admission differs; stage 210 identifies the rejection below.
Retained TX and actual RTOS/DMA ownership are still incomplete. See
[cooperative close coverage](parity.md#cooperative-socket-close).

Stage 207 compares low-level stream admission and publication in 1,206 original
scenarios. It covers duplicate-direction rejection, selected capacity checks,
TX-before-RX allocation, partial-allocation cleanup and publication ordering.
All 47 native sanitizer suites and 18 Python tests pass. Factory internals and
host handle/worker creation remain outside this comparison; the stock
spare-port rejection was unresolved at this stage. This is source-only work:
the standard build reproduces stage 206. A fresh 30-second HDMI-to-USB capture shows the camera
scene, with 33 linked samples and zero sampled socket drops. Stage 206 remains
running in RAM. See [stream admission coverage](parity.md#stream-admission-and-publication).

Stage 208 compares host-facing handle admission, including role checks, host-port
mapping, first-free reservation, encoded handle IDs and full-table cleanup.
789 original scenarios include 32 composed open/lookup/close sequences. Four
sanitizer suites and 18 Python tests pass. The helper remains source-only; factory
internals, table initialization, worker startup and the stock spare-port rejection
were still open at this stage. The build reproduces stage 206, retained in RAM.
A fresh 30-second HDMI-to-USB capture shows the camera scene with 33 linked samples
and zero sampled socket drops. See [handle admission coverage](parity.md#handle-admission-and-open-cleanup).

Stage 209 compares stream and queue factories in 468 original scenarios,
including 16 creation/close sequences. A separate 60 overflow probes establish
an intentional native guard against undersized wrapped allocations. Retained
queues use a fixed 100-byte capacity. Six sanitizer suites and 18 Python tests
pass, including real-storage write/acquire/release/close composition. The helpers
remain source-only; heap/RTOS internals and worker startup are still outside
coverage. The build reproduces stage 206. A fresh 30-second HDMI-to-USB capture
shows the camera scene with 33 linked samples and zero sampled socket drops;
stage 206 remains running in RAM. See [factory coverage](parity.md#stream-and-queue-factories).

Stage 210 identifies the stock socket-admission difference: both directions
behind host ports 0 and 1 already have streams. All seven selected opens return
inner SDIO error `-9`, wrapped as `-259`; the original daemon forwards only the
outer status. Stock results agree with and without a temporary daemon observer.
Open8030 admits all seven opens, confirming missing internal ownership in the
runtime. The specific internal owners and their data paths remain unimplemented;
rejecting those ports alone would not close the gap. See
[stock stream occupancy](parity.md#stock-stream-occupancy-and-internal-interfaces).

The firmware image is unchanged. Its first warm reload produces black camera
output despite a linked radio and delivered video bytes, so that session fails
acceptance. A full stock reboot and reload of the same Open8030 image restore
the visible scene. Repeating all seven native opens/closes during a fresh
30-second HDMI-to-USB capture passes with 33 linked samples and zero sampled
socket drops. The observer is removed and stage 206 is retained in RAM. The
warm-restoration cause, controlled freshness and measured latency remain open.

Stage 211 traces mode-3 internal provisioning in 64 original-instruction
scenarios. Socket port 0 serves UART1/SBUS; socket port 1 routes to the event
bridge on DEV and UART2 on AP. A new source-only event-forwarding helper matches
155 original cases, including cached buffers and allocation failures. It adds
a bound for the original unchecked copy. Two sanitizer suites and 18 Python tests
pass. Runtime event subscriptions and internal services are still unintegrated.
The build reproduces stage 206; a fresh 30-second HDMI-to-USB capture shows the
camera scene, with 33 linked samples and zero sampled socket drops. The accepted
image remains running in RAM. See [internal data coverage](parity.md#mode-3-internal-data-graph-and-event-forwarding).

Stage 212 adds a source-only parser for internal socket port 1. It matches
893 original scenarios across 3,118 feed calls, covering fragmentation, ring
wrap, CRC8-D5, malformed-data recovery and allocation failures. One extra guard
rejects an exactly full ring, following the original worker's reserved-byte
policy; it is recorded separately from matching behavior. Two sanitizer suites
and 18 Python tests pass. Other profiles, runtime routing and internal services
remain open. The build still reproduces stage 206; a fresh 30-second
HDMI-to-USB capture shows the camera scene with 33 linked samples and zero
sampled socket drops. The accepted image remains running in RAM. See
[framing coverage](parity.md#internal-socket-port-1-framing).

Stage 213 integrates all twelve original host event subscriptions and declared
sizes, with a shared 4 KiB copied FIFO. Unsubscribe now preserves queued
notifications. Nineteen original sequences (157 actions and 24 notifications)
match, and 56 selected hardware responses match stock status/payloads. One
sanitizer suite and 18 Python tests pass. Fresh stock 20-second and native
45-second HDMI-to-USB captures show the camera scene; all 48 native samples
stay linked with zero sampled socket drops. Stage 213 is retained in RAM and
recorded as the accepted build. Event producers beyond 0/1 and internal routing
remain pending. See [host event coverage](parity.md#host-event-subscriptions-and-copied-transport).

Stage 214 adds a source-only comparison of internal route delivery: all 64
destination positions, direct/queued writes, reference transfer, failure paths,
counter wrap and event-bridge composition. It matches 284 original cases,
including 96 bridge cases. The matched mode-3 routes preserve the full framed
packet; event-buffer allocation failure can count as a successful write without
emitting an event. Queued writer completion and runtime integration remain open.
Three sanitizer suites and 18 Python tests pass. The build reproduces stage 213;
a fresh 30-second direct HDMI-to-USB capture shows the camera scene with all 33
samples linked. The video socket drops another 108,063 bytes during that check,
so the no-drop check fails despite visible video. The cause and stock comparison
under the same load remain open. Stage 213 remains running in RAM.
See [route delivery coverage](parity.md#selected-internal-route-delivery).

Stage 215 instruments the video receive/drop path and SDIO descriptor lifetime.
The stage-213 session also loses 6,452 bytes during a quiet 45-second interval
with only start/end counter reads, so frequent observation is not required for
loss. After a fresh stock boot, the instrumented image passes 420 flow samples
with no dropped bytes or SDIO errors. Its video queue peaks at 12,231 of 56,320
bytes; the longest poll-observed SDIO descriptor duration is about 13.6 ms.
Fresh stock and native HDMI-to-USB captures show the camera scene. This is an
instrumentation checkpoint, not a streaming fix: restart and instrumentation
effects are not isolated, and the intermittent loss remains open. See
[flow measurements](parity.md#video-receive-flow-measurements) and the
[diagnostic layout](architecture.md#host-protocol).

Stage 216 compares queued internal destination writing against 159 original
cases, including 96 event-bridge cases. It checks mutex/callback/release ordering,
deferred accounting, failures and already queued work after disabling a
destination. Three sanitizer suites and 18 Python tests pass. The helper remains
source-only and the build reproduces stage 215. A fresh 30-second direct HDMI
capture shows the camera scene with all 33 samples linked; a separate ten-minute
check has 600 flow samples with zero socket drops and SDIO errors. Stage 215
remains running in RAM. The earlier intermittent loss and runtime internal
service integration remain open. See [queued writer coverage](parity.md#queued-internal-destination-writer).

Stage 217 compares the host ingress callbacks for internal event-interface ports
0/1/2. It matches 460 data cases and 765 control-delegation cases; 24 wrapped-length
cases record an intentional native guard difference. Parser and common-control
semantics remain outside this comparison, and these setters remain unsupported
at runtime. One sanitizer suite and 18 Python tests pass; the build reproduces
stage 215. A fresh 30-second HDMI-to-USB capture shows the camera scene with all
33 samples linked and no sampled socket drops. Stage 215 remains running in RAM.
See [host ingress coverage](parity.md#host-to-internal-data-ingress).

Stage 218 compares framing on parser objects created by the original constructor.
Event port 0 and socket port 1 share the C8 profile, but their parser rings hold
1,043 and 524 bytes respectively. The socket's separate RX queue holds 1,024 bytes;
this corrects the earlier parser-capacity description. There are 334 matching
scenarios / 2,718 feeds and two explicit exact-fill guard differences. One
sanitizer suite and 18 Python tests pass. The helper remains source-only and the
build reproduces stage 215. A fresh 30-second HDMI-to-USB capture shows the camera
scene with all 33 samples linked; 60 flow samples have no socket drops or SDIO
errors. Stage 215 remains running in RAM. See
[constructed parser coverage](parity.md#constructed-internal-parser-profiles-and-capacities).

Stage 219 compares the internal socket writer and port-1 TX occupancy gate.
It matches 871 original cases, including 294 executions of the selected queue
status getter path. Nonzero roles suppress port-1 writes above 32 queued bytes,
or 262 when the mode field equals 1. One sanitizer suite and 18 Python tests pass.
The helper remains source-only and the build reproduces stage 215. Fresh HDMI
video remains visible with 33 linked samples; 60 flow samples show no socket
drops or SDIO errors. Stage 215 remains running. See
[internal write gate](parity.md#internal-socket-write-gate).

Stage 220 compares original interface/route construction, parsing, selection,
queue messages and delivery together. All 108 cases / 3,540 feeds match the
native helpers composed with a test-only selection adapter: 54 packets are
delivered and 54 cases have no matching route. Destination bytes, event payloads,
counters and final packet references match. One sanitizer suite (1,698 split
cases) and 18 Python tests pass. Production route construction and internal
service ownership remain open. The build reproduces stage 215; a fresh
HDMI-to-USB capture shows the camera scene with 33 linked samples, and 60 flow
samples show no socket drops or SDIO errors. Stage 215 remains running. See
[composed route coverage](parity.md#constructed-parser-to-route-composition).

Stage 221 adds native predicates for an admitted route: source/second-interface
identity, metadata range and byte-pattern matching. There are 649 matching
original cases and 45 documented native bounds-guard differences. The 108
composed traces still match with native source selection. Two sanitizer suites
and 18 Python tests pass. Construction, complete route-list handling and runtime
ownership remain open. The build reproduces stage 215; fresh HDMI-to-USB video
shows the camera scene with 33 linked samples, and 60 flow samples show no socket
drops or SDIO errors. Stage 215 remains running. See
[route predicate coverage](parity.md#admitted-route-selection-predicates).

Stage 222 adds native route-list submission, worker mapping and action-group
suppression. All 227 original cases match, including queue/copy failures and
counter wrap. Three sanitizer suites and 18 Python tests pass. Copying actions
retain an allocation reference at the submission boundary; downstream lifetime
and action processing remain open. The helper remains source-only and the build
reproduces stage 215. Fresh HDMI-to-USB video shows the camera scene with 33 linked
samples; 60 flow samples show no socket drops or SDIO errors. Stage 215 remains
running. See [submission coverage](parity.md#route-list-submission-and-action-suppression).

Stage 223 adds native worker packet transformations and compares them with direct
route delivery. There are 211 matching original cases, 116 explicit native bounds
and headroom guard differences, and six original allocator probes. The allocator
starts with one reference; the tested worker releases one. Pool recycling and
complete copied-packet lifetime remain unresolved. Four sanitizer suites and 18
Python tests pass. The helper remains source-only and the build reproduces stage
215. Fresh HDMI-to-USB video shows the camera scene with 33 linked samples;
60 flow samples show no socket drops or SDIO errors. Stage 215 remains running.
See [worker actions](parity.md#worker-packet-transformations-and-reference-boundaries).

Stage 224 composes submission and worker delivery with the original allocator,
reference operations and pool recycling. All 170 scenarios / 776 cycles match,
plus 1,552 empty-receive checks. Non-copying paths recycle their buffers; copying
paths retain one reference after the tested workers drain and can exhaust copy
capacity in the synthetic pool. Normal-constructor reachability and cleanup
elsewhere remain unresolved. Five sanitizer suites and 18 Python tests pass. The
new worker helper remains source-only; the build reproduces stage 215. See
[composed lifetime coverage](parity.md#submission-worker-delivery-and-original-pool-recycling).
Fresh HDMI-to-USB video shows the camera scene with 33 linked samples; 60 flow
samples show no socket drops or SDIO errors. Stage 215 remains running.

Stage 225 adds worker discard control, modular queue-age accounting and the
original missing-packet/action errors. All 357 new cases match, and the retained
170-scenario / 776-cycle composition still matches. Five sanitizer suites and
18 Python tests pass. This remains source-only; the standard build reproduces
stage 215. Normal lifecycle configuration and runtime ownership remain open. See
[worker control coverage](parity.md#worker-discard-control-queue-age-and-errors).
Fresh HDMI-to-USB video shows the camera scene with 33 linked samples; 60 flow
samples show no socket drops or SDIO errors. Stage 215 remains running.

## Remaining work

[Original firmware parity](parity.md) tracks the comparison scope, known host
request and PHY-command gaps, and the evidence required to close each area.
Full original-firmware coverage has not been established.

- Dynamic host power controls and broader channel, band and RF policy support.
- Complete telemetry/host-command parity and broader time-synchronization modes.
- Internal data-interface provisioning, UART/SBUS service mapping, shared stream
  ownership and complete socket admission/teardown behavior.
- Wider modes, other hardware variants and transmitter support.
- Physical peer-silence/restart, pending-DMA recovery and extended operation tests.
- Sustained-session video receive/backpressure: stage 214 observes dropped bytes
  in the unchanged accepted image while video remains visible.
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
