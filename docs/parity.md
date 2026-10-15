# Original firmware parity

**Full original-firmware coverage is not established.** Working camera video is
one acceptance test. It does not establish command, timing, RF-policy, recovery
or telemetry compatibility. This checklist tracks the remaining comparison work;
it is not an exhaustive inventory of the original firmware yet.

## Reference and scope

The reference is the Ascent VRX running the matched 18.21.10 software and board
inputs. Keep the original Linux daemon, receiver backend, loader configuration
and hardware fixed when comparing the radio firmware.

| Artifact | SHA-256 |
| --- | --- |
| Original radio container | `dc58247910af564376350404383c5c411cbec21663873fba24e70c61a3584e3c` |
| Original main payload | `3e454131a334ec422c0b2e8ae4924d6bda68cb61b70fd754da26412578b39c87` |
| Accepted Open8030 container at the start of this audit | `fc8d2b94c336f0718f93d28811a36747cb04eeb8409acf99973df720af8fe552` |

The original main payload maps to `0x204000`, at container offset 17,920.
Synthetic analysis ELF headers, symbols and ABI assumptions are analysis metadata,
not original declarations. Keep original binaries, instruction exports, captures
and detailed comparison receipts in ignored `.local/`.

The immediate hardware target is this receiver. Other original roles, modes,
bands and boards remain explicit gaps; success on this target cannot close them.
Transmitter evidence must be collected separately with its own release and board
provenance. Replacing the Linux video/backend software is a separate project.

## How a coverage item is closed

Track these independently for every command, message and state transition:

1. **Original contract:** establish dispatch, input/output layout, error results,
   state preconditions, timing and side effects against hash-matched original
   instructions. A recovered function name or decompilation is only a lead.
2. **Offline comparison:** compare independently implemented behavior with the
   original where executable, recording every intercepted helper and peripheral
   assumption. Include boundary values, malformed input, repeated requests,
   wrong states, arithmetic wrap and ownership/queue limits. Native unit tests
   alone are not an original-firmware comparison.
3. **Runtime integration:** exercise the normal loader and Linux backend, not
   only a direct diagnostic call. Check responses, notifications, actual hardware
   effects and interactions with outstanding work.
4. **Hardware parity:** run the same scenario on stock and Open8030, record exact
   image/configuration hashes and boot/session identity, and inspect camera output
   after each firmware iteration. A link flag, byte counter or live overlay alone
   is insufficient.

If video breaks, fix it immediately or restore the accepted image before the next
candidate. A failed candidate is not a new baseline. Retain the candidate image,
comparison results, capture settings and rollback outcome privately; commit
accepted changes with their validation limits.

## Coverage matrix

“Partial” means some behavior exists or has evidence, with remaining work listed
in the last column. Historical hardware results apply only to the tested image
and scenario; they must be repeated after relevant changes.

| Area | Current Open8030 coverage | Required comparison / remaining gap |
| --- | --- | --- |
| Original behavior inventory | Core API routing inventoried; full system inventory incomplete | Resolve command contracts, transport dispatch, callbacks, timers, configuration options and reachable modes. Unmapped behavior blocks a full-coverage claim. |
| Container, bootstrap and loader | Working matched-board baseline | Repeated cold starts; bad/truncated configuration and failed preparation; ordering and recovery across original loader paths. |
| OTP, flash and factory data | Identity and read-only lookup; hardware covers zero-offset fallback | Valid factory records, partition variants, corrupt records and hardware-calibrated offsets. |
| Host framing and SDIO | Framing, enumeration and cooperative transport | Original error/reset semantics, saturation, concurrent traffic, reconnect and interrupted DMA. |
| Host getters | Main status, DEV AP time and baseband register read | Complete field semantics, all remaining getters, unavailable states and original error responses. |
| Host setters and configuration | Candidate list and restricted loader configuration | Full dispatcher inventory, dynamic power, channel/band policies, reset/default behavior and runtime configuration changes. |
| Host lifecycle | Explicit stop/start and stop/deinit/init/start tested historically | Every wrong-state/repeated operation, cancellation, pending DMA, failure recovery and complete reset semantics. |
| Events | Subscription and delivery for IDs 0 and 1 | Other original IDs, payload layouts, ordering, cancellation and overflow behavior. |
| Host sockets | Slot-0 copy-mode data path; normal backend video/control | Other modes/slots, retained-descriptor TX, backpressure, teardown races and original capacity/error behavior. |
| MAC framing | Header/descriptor parser and DEV encoder | Other formats/flags, receive ordering, duplicate handling and all original rejection rules. |
| AP full/compact controls | Selected mode-0/slot-0 admission and connected control | All extensions, remote synchronization effects, changed channels, slots, stale/duplicate controls and unsupported modes. |
| Internal port-0 commands | Stream parsing; login checksum ACK transmission | Receiving a command currently mainly records it. Inventory and implement each original command's effects, responses and retries. |
| PHY commands | See command table below | Exact callbacks, TTL/replacement ordering and side effects for every command. |
| Hardware command preparation | Command builder, metadata updates and preparation selection; fixed DEV frame, cached board/TX calibration and TX look-ahead | Independent sweep and base-channel score arithmetic are integrated; active-catalog policy, auxiliary scans, PA-sleep policy, retained-buffer ownership and actual interrupt ordering remain open. Helper comparisons do not establish the complete original handler. |
| MCS adaptation | RX adaptation, normal-key user-0 TX increase/decrease drain and attempt history integrated; peer-requested decrease observed on hardware | Broader decrease scenarios, queue delivery/ownership, all PHY keys, retry/expiry, loss interactions and sustained adaptation. |
| Power and calibration | Initial user-0 setup at level 25; calibration writes, cache ordering and band-offset helpers compared offline | Level 11 also fails acquisition in the accepted image with only its loader setting changed. Resolve the low-power transmit path, then normal backend power writes, all users, peer policy and calibration effects. |
| Channel/band selection | Configured link search, independent measurement sweep and base-channel averaging | Match the original active catalog and sampling cadence; integrate automatic selection, independent uplink changes, subchannels, hopping, rollback and cross-band operation. |
| Time synchronization | DEV AP-time decoding/getter; original-instruction comparison and hardware startup/restart checks | Physical peer-loss/stale time, long-run hardware wrap and other roles/modes. |
| Link recovery | Candidate removal/readmission and bounded recovery machinery | Physical peer silence/restart, pending work, sustained reacquisition and repeated failures. |
| Video/control/telemetry | Visible camera video and byte transport | Freshness challenge, measured end-to-end latency, telemetry values, control effects, loss/throughput and prolonged operation against stock. |
| Platform and resources | Cooperative polling; caches/interrupts disabled | Complete interrupt/timer behavior, long-run wrap, resource margins, faults and recovery under load. |
| Other roles/boards | Unverified | Separate original inventories and hardware validation; no inferred VRX-to-VTX parity. |

## Host request coverage

### Core API and reply-size inventory

The [command checklist](host-command-coverage.csv) records **114 distinct IDs**
across the matched original core dispatcher and reply-size table. Of these,
**108 have recognized core routes**. Every ID has a separate row, including
commands whose behavior is still unresolved. These are inventory counts, not
implemented features or a firmware coverage percentage.

| Core family | Recognized IDs | Current native coverage |
| --- | ---: | --- |
| Initialization/configuration (`0x00…`) | 12 | Direct commands unsupported; loader JSON is a separate, partial configuration path |
| Getters (`0x01…`) | 35 | Status, DEV AP time and register read have partial behavior/scenario coverage; 32 unsupported |
| Setters (`0x02…`) | 56 | Candidate list has partial coverage; 55 unsupported |
| Extension (`0x06000000`) | 1 | Unsupported; original callback contract unresolved |
| Lifecycle (`0x0b…`) | 4 | Start, stop, init and deinit have partial behavior/scenario coverage |

The inventory follows all four command tables, their range checks, the top-level
family dispatch and explicit extension/lifecycle branches. Original-instruction
execution matches 4,811 routing cases, including every table slot, extension IDs,
initialization gating and boundary samples for all 256 high-byte families. The
98 executed instruction locations and table bytes are bound to the original
payload, container and synthetic ELF. No command handlers or hardware effects
execute in this comparison; input/output pointers and initialization state are
synthetic. The bounds were inspected as well as sampled; this is not an execution
of all 2³² possible IDs.

The original reply-size lookup has **99 entries**, with 93 also recognized by
the core dispatcher. Its instructions match 1,537 lookup cases, including every
entry and missing-ID cases; all 16 instruction locations are bound to original
bytes. The checklist records these declared reply lengths, not a guarantee that
every returned byte is initialized or meaningful. An empty length means no table
entry; `0` means an explicit zero-byte reply. Request-length validation, allocation
failures and response serialization remain separate comparison work.

The mismatches are retained explicitly:

- `0x0100006e`, `0x0100006f`, `0x02000017`, `0x02000019`, `0x0200001a` and
  `0x0a000004` have reply-size entries but reach the core default rejection.
  This does not rule out handling by another layer.
- Eleven initialization/configuration IDs and setters `0x02000000`,
  `0x02000001`, `0x02000071` and `0x02000074` have core routes without reply-size
  entries. Their availability through the ordinary host transport cannot be
  assumed. Initialization ID `0x00000002` does have a size entry.

Recognition does not guarantee success: some entries depend on an installed
callback, valid arguments or runtime state. The original core default returns
`-4`; wire-level error mapping must be checked separately. Native implementation
classifications follow the accepted source dispatcher, not an assumption that
an observed request succeeded.

These inventories are **not the whole host protocol**. Transport framing and
request validation, events, sockets, daemon-local operations and reset/heartbeat
routing need their own inventories. In particular, an ID absent here may be
handled by another layer. Core initialization IDs are not automatically
host-accessible commands.
Indirect callbacks, internal radio messages and timers also remain open; finding
all IDs in this dispatcher does not close those behaviors.

### Selected runtime probes

The selected read-only probe on 2026-10-15 UTC, using the accepted container above,
returned:

| Request | Meaning | Native result |
| --- | --- | --- |
| `0x01000000` | Status | Success, 300 bytes; this does not validate every field |
| `0x01000006` | RX MCS | Unsupported (`-95`) |
| `0x01000007` | Power mode | Unsupported |
| `0x01000008` | User-0 power level | Unsupported |
| `0x0100000c` | AP time | Unsupported |
| `0x01000069` | System information | Unsupported |
| `0x0100006b` | Link information | Unsupported |
| `0x01000071` | Factory offsets, band 0 | Unsupported; boot-time lookup is a separate path |

This is **eight selected requests**, not an exhaustive getter inventory or a
compatibility percentage. Repeating four getters six times is 24 observations,
not coverage of 24 distinct commands.

The running firmware's audit also contains the following requests. It records
request IDs/counts and the most recent input length, not response outcomes or
caller identity. Some requests originate from observers. Unsupported classifications
below follow the current source dispatcher, not the audit alone.

| Requests seen in the session | Current handler / gap |
| --- | --- |
| `0xff000000`, `0xff000001`, `0xff000002` | HELLO, synchronization reset and heartbeat; reset does not implement a full radio reboot |
| `0x02000004` | Candidate list; bounded supported profile |
| `0x03010000`, `0x03010001` | Supported event subscriptions |
| `0x03010002`, `0x03010008`, `0x0301000c` | Unsupported event subscriptions |
| `0x04000002`, `0x04000003`, `0x04010002` | Socket open/write used by the backend |
| `0x01000005`, `0x0100000a`, `0x01000012`, `0x010000c8` | Additional unsupported getters; complete original contracts still need tracking |
| `0x02000008`, `0x02000009`, `0x0200000c`, `0x0200000d`, `0x02000010`, `0x02000020`, `0x02000024`, `0x020000c8` | Unsupported setters; inventory each original effect before implementing or probing writes |
| `0x05000000` | Unsupported request; original contract unresolved here |

The native host audit has 64 entries; an overflow makes it incomplete even for
the observed session. Zero overflow does not prove that every original command
has been exercised. Project diagnostic IDs (`0x015a…` / `0x0c5a…`) are not vendor
compatibility coverage. Daemon-local commands must be distinguished from commands
forwarded to the radio.

## PHY command coverage

These seven-byte PHY messages are separate from host requests and internal port-0
commands. Equal command numbers across those layers have no implied relationship.

| PHY command | Current effect | Remaining coverage |
| --- | --- | --- |
| 0 | Compared cache semantics; synthesized admission reports unsupported | Mode-3 reconfiguration/admission and broader local-position state |
| 1 | Unsupported | Original callback and hardware effects |
| 2 | Unsupported | Original callback and hardware effects |
| 3 | Normal-key user-0 TX increase/decrease drain and attempt history; outgoing RX-MCS request | Broader hardware decrease scenarios, full ownership, retry and timing/acknowledgement behavior |
| 4 | Unsupported at runtime; callback, transaction and monitor helpers compared offline | Integrate PHY transport, channel transition, confirmation and rollback; verify physical retuning and failure recovery |
| 5 | Unsupported | Original callback and hardware effects |
| 6 | Selected RX/TX identity fields | All user/state combinations and shared-field effects |
| 7 | DEV AP-time offset and host getter | Physical peer-loss/stale time, long-run hardware wrap and other roles/modes |
| 8 | Validated format only | Original callback and hardware effects |
| 9 | Compared format/cache/timer handling; original descriptor has no callback | Hardware-triggered timing and cache-replacement scenarios |

See [wireless protocol](wireless-protocol.md#phy-control-messages) for existing
length, checksum and TTL handling. Format acceptance is not execution coverage.

## Evidence limitations and next work

The 2026-10-15 audit confirmed native identity and linked state before and after
the selected getter probes. Camera output was inspected through the VRX's HDMI
output and the host's HDMI-to-USB adapter at `/dev/video0`, recorded directly as
1920×1080 MJPEG at the adapter's nominal 30 fps. A separate capture-service
recording selected a different camera and was excluded from VRX evidence.
Adapter cadence cannot establish camera/RF frame rate or end-to-end latency.
This is a current native regression check, not a fresh stock/native comparison.

### AP-time implementation after the initial audit

The initial getter probe above remains a record of the earlier image. The next
candidate implements PHY command 7 and getter `0x0100000c`; see
[AP-time synchronization](wireless-protocol.md#ap-time-synchronization).
Its decoder/getter matches 2,304 original-instruction cases covering all 32
header/TTL combinations, payload-tail variations, millisecond boundaries and
32-bit wrap. Clock and semaphore/PHY acquisition helpers are intercepted, while
the original parser, checksum, copy, division and getter instructions execute.

Hardware checks include a fresh stock AP-time/video reference, then native
startup, stop/start and stop/deinit/init/start. The native getter advances,
retains its offset through stop/start, returns the unavailable sentinel after
reinitialization and synchronizes again after reception starts. Direct HDMI
captures show the camera scene after each sequence. This closes the implemented
DEV clock path for these scenarios, not every time-related behavior in stock.
The accepted image and hardware limits are recorded in [status](status.md) and
[baseline.json](../baseline.json); detailed receipts remain private.

### Register-read command

`0x01000064` accepts two little-endian 16-bit values: byte offset and byte count.
The original allows unaligned windows, counts from 0 through 256, and an end
offset no greater than 4,096. Each returned byte performs its own aligned
32-bit register read. A changing register can therefore contribute bytes from
different instants; the reply is not an atomic snapshot.

Valid four-byte requests return a 256-byte payload, including range errors
(`-6`). Open8030 zeroes the unused tail and error payload instead of returning
previous buffer contents. Malformed native requests return `-22` without a
payload; pre-probe native reads return `-11`. Those additional validation paths
are not claimed as exact original behavior.

The native read function matches 360 original-instruction cases for statuses,
requested bytes and read addresses/counts, with 99 instruction locations bound
to original bytes. Cases include changing synthetic registers, unaligned reads,
empty/full windows and boundary errors. Clock, time-division and logging helpers
are intercepted on the original error path. Allocation, serialization and actual
peripheral read effects are outside this offline comparison.

Hardware comparison covers eight stock/native success/error requests, including
fixed reply sizes and the active receive PLL bytes. Native checks also cover four
malformed lengths and fourteen 256-byte register windows while video is running.
The 30-second HDMI capture shows the same dark textured camera scene as fresh
stock; all 33 native samples remain linked, with advancing AP time and zero
sampled socket drops. This does not establish all register side effects, concurrent
state transitions, sustained polling limits or clear-scene video quality.

### Isolated low-power acquisition failure

Changing only temporary loader `power.manu_init` from 25 to 11, with the accepted
AP-time firmware bytes unchanged, failed acquisition: 18 samples show states 0/1,
unavailable AP time and no delivered video. The HDMI capture shows the disconnected
receiver background. A full reboot and restoration of the original configuration
restore link state 2 and the dark camera scene also seen on stock.

This reproduces failure without the shelved dynamic-power implementation. It
narrows the problem to existing low-power acquisition behavior; it does not identify
the defective RF setting, prove emitted power or validate the dynamic setter.
The temporary configuration was discarded. The retained baseline still uses 25.

### Command-preparation contract investigation

The original preparation handler and its board callback were executed in 3,840
synthetic cases, covering prepare modes 0/3, five raw-user inputs, empty and
nonempty command rings, cursor positions, four cached calibration states,
board-cache reuse and the post-callback bypass flag. All 301 executed instruction
locations are bound to the matched original payload, container and analysis ELF.
A separate contract
model matches the selected cursor, callback arguments, ordered write arguments
and resulting caches. **This is original-behavior evidence, not a comparison
with the current native handler.**

The cases establish that preparation depends on the selected command metadata,
not just a raw-user number or configured power level. For the tested rings,
only TX metadata causes calibration-bank changes. Reusing the cached bank skips
those writes; starting with cache value 3 also enables compensation. The board
callback runs before the bypass flag suppresses later preparation work. The
mode-dependent buffer-control writes are another effect missing from the current
native preparation path. See [command preparation](wireless-protocol.md#hardware-command-preparation).

Register writes are recorded at intercepted helpers; actual peripherals, IRQ
timing and emitted RF are not emulated. Calibration bytes and ring contents are
synthetic. Other IRQ modes, scan callbacks and the board's PA-sleep branch remain
outside this trace. These results neither locate the low-power fault nor justify
changing individual registers from a stock snapshot.

No firmware candidate was loaded for this investigation. A direct 10-second
HDMI-to-USB capture of the retained register-read image still shows the dark
camera scene, with 13 linked samples, valid AP time and zero sampled socket
drops. Lighting, controlled freshness and clear-scene quality remain unverified.

### Calibration lifetime across normal restart

The current native image preserves loaded high-band TX compensation through
normal stop/start and stop/deinit/init/start. Previously, rebuilding the receive
context cleared these fields and its software cache, leaving an interval before
the next TX preparation with compensation disabled. Probe, default loading,
calibration and transitions to/from diagnostic modes still invalidate the cache.

An independent offline comparison executes four original successful engine-reset
helpers across 128 cases and binds 127 instruction locations to the matched
original bytes. These cases retain the original calibration cache and seeded
compensation fields. Peripheral acknowledgement is simulated and field writes
are intercepted: this does not execute the complete original lifecycle task graph
or establish hardware reset effects independently. The previous preparation
trace supplies the separate cached-bank contract.

Native regression tests count writes as well as checking final values, so a
clear-and-reload cannot conceal the disabled interval. The new tests fail against
the previous native implementation and pass with the fix. They cover pre-encode
before the next preparation, normal restart, recovery and diagnostic transitions.
Hardware stop/start and stop/deinit/init/start preserve the five observed
compensation words and restore the stock-matching dark camera scene. This does
not close multi-bank preparation, low-power acquisition or RF-power accuracy.

A planned hardware candidate-removal test stopped before mutation: the large
native RX diagnostic received an unexpected backend response. The cause remains
unresolved at that point; the following diagnostic change enabled the hardware
check described below. A warm stock restoration followed by candidate reload also
reproduced the known disconnected HDMI output. A full receiver reboot restored
stock video before the final candidate reload and inspection.

### Daemon response limits and recovery observation

The matched original Linux daemon's encoder and TCP forwarding wrapper were
executed in 27 offline cases at payload-size boundaries. All 99 executed
instruction locations are checked against the daemon binary with SHA-256
`63f16bb229a4091e3d5fc00537135a2f8bb692a3042ff27bde8009dd1bf5cefc`.
The wrapper provides 4,096 bytes to the encoder; payload plus 19 bytes of framing
must fit. With the former 4,084-byte RX diagnostic, encoding returns `-1` and the
wrapper passes that value as the unsigned length to `send`. The harness intercepts
`memcpy` and `send`; it neither executes the kernel send nor models daemon threads.
This establishes an oversized-response defect without proving the exact source
of the earlier unrelated backend response.

Open8030 now requires bounded ranges for its RX diagnostic daemon alias. It
returns at most 1,024 bytes and rejects empty, malformed and out-of-range requests
without payload. The complete record remains available on the direct transport.
These are project diagnostic contracts, not newly implemented vendor commands.
Hardware checks cover ten complete records read in ranges, eleven invalid
requests and both zero-length boundary reads, while camera video continues.

The range reads and current host-audit record identify the active peer and a
restorable candidate list. Removing and restoring that list triggers recovery:
the candidate-removal and completed-recovery counters advance, the failure count
does not, and the five compensation words remain unchanged while removed and
linked again. Startup and post-recovery HDMI captures total 35 seconds, with 41
linked/AP-time samples and zero sampled socket drops. Both show the dark textured
camera scene also seen on fresh stock. Physical peer loss, pending DMA, repeated
recovery stress and controlled scene freshness remain open.

### Command construction and retained metadata

Normal DEV initialization now installs its three-descriptor frame through a
command builder: broadcast RX, independent measurement and local TX. Expansion
writes prepare/body slots, user nibbles, the terminator and displaced pre-command
fields while execution is held. It retains up to 20 metadata entries, preserving
fields the original leaves untouched. Search retuning updates their frequency
and bank, and native profile writes update the first matching entry's bandwidth
or RX flag. This construction step did not implement cursor selection, board callbacks or
scan actions. The subsequent preparation integration is recorded below.

A fresh execution of original builder `0x21dfea` and updater `0x2212f6` supplies
568 construction cases and 6,720 updates. Native comparison matches all retained
metadata and 179,867 ordered field writes. Six supporting original instruction
ranges, including raw-user mapping, profile getters and frame composition, are
revalidated against the matched payload bytes. Cases cover 0–47 descriptors,
roles, signed displacements, retained fields and first-match update behavior.
Allocation, interrupt locks, profile getters and MMIO helpers are intercepted;
these synthetic comparisons do not execute the command engine or establish
original allocation-failure behavior.

Native sanitizer tests cover invalid-input rejection before writes, reserved
bits, empty programs, the normal frame, channel retuning and lifecycle rebuilds.
Hardware reads compare the active command bytes, user nibbles and all pre-command
slots with freshly booted stock. Startup, stop/start and deinit/init/start video
captures total 50 seconds with 59 linked/AP-time samples and zero sampled socket
drops; all show the same dark camera scene as stock. Only the existing fixed
DEV frame and shared frequency have hardware coverage. Broader frame composition,
preparation IRQ effects, controlled freshness and low-power acquisition remain
open.

### Command-preparation integration

The normal DEV IRQ now selects retained command metadata before applying
preparation effects. It preserves the original fast-path raw-user check and
fallback raw-user/hardware-mode check. Unmatched notifications produce no
preparation writes. TX metadata selects calibration; the board frequency-region
cache and calibration cache suppress repeated programming. Body notifications
leave the cursor unchanged and apply the next TX entry's bandwidth look-ahead.

The native helper matches 14,864 original-instruction cases and 15,956 ordered
write arguments. All 460 executed instruction locations are bound to the
matched original bytes. Comparisons cover empty rings, stale cursors normalized
on preparation, fallback matches/misses, all three synthetic calibration banks,
cache reuse, six board regions and their boundaries, bypass ordering, both roles,
TX-buffer flag consumption, work-mode flags and all eight look-ahead bandwidths.
Register writes, time/logging, memory copy, buffer lookup and work-mode lookup
are intercepted. Non-preparation scan helpers are intercepted without effects;
this explicitly excludes their behavior from parity. Board PA-sleep flags are
clear throughout. No actual peripheral timing or emitted RF is emulated.

Runtime integration uses only the existing fixed mode-0 DEV frame and calibrated
bank 2. Other bank data is unavailable and is never replaced by bank 2. The
copy-mode TX path does not yet supply the original retained-buffer ownership
flags to this helper. Scan actions and PA-sleep policy remain open. Sanitizer
regressions cover out-of-order preparation, first-load versus cache reuse,
unavailable-bank handling and cache retention across normal lifecycle changes.

Fresh stock and native command windows match byte-for-byte. Native startup,
stop/start, deinit/init/start and candidate removal/readmission preserve the five
observed compensation words and restore the dark textured camera scene seen on
stock. The four HDMI-to-USB captures total 70 seconds, with 82 linked/AP-time
samples and zero sampled socket drops. The recovery counter advances without a
failure. These observations do not establish controlled freshness, clear-scene
quality, emitted power, physical peer-loss recovery or all IRQ timing.

### Independent measurement scan

The normal DEV body notification before raw user 13 now advances the measurement
channel and tunes only logical user 11 RX. The five PLL bytes occupy the shared
special-user shadow at `0x90a`; link RX8 and TX0 retain their own shadows. The
measurement command's frequency/bank metadata follows this tune. Link search
restores the independent measurement context after rebuilding shared settings.
The configured catalog is retained even when acquisition starts on a fixed channel.

Native channel advancement composed with command selection matches 7,808
original scheduler cases, including 1,696 scan actions. Original catalog iteration,
subchannel advancement and configuration lookup execute; board callbacks and tune
requests are captured. A separate user-11 RX tuning comparison matches 66 cases
and 528 ordered writes. Its PLL conversion returns synthetic 40-bit words; it
validates byte placement, band controls and metadata-update arguments rather than
PLL arithmetic. The combined 409 executed instruction locations are checked
against the matched original bytes. Other users, physical RF and actual IRQ timing
are outside these comparisons.

The polling runtime adds consistency checks before tuning and a tune/prepare/sample
sequence before assigning a power event to a channel. Pending, duplicate or
unconfirmed events are excluded from per-channel counts. These counts are project
observations, **not the original averaged scan scores**. This initial sweep did
not implement scoring; the subsequent [score-processing work](#base-channel-score-processing)
adds it for confirmed events. Automatic selection, subchannel processing and
auxiliary user-12 scans remain unimplemented in the runtime. The small project diagnostic `0x015a0027` exposes scan progress and
excluded events; it is not a vendor command.

Hardware sampling shows the measurement PLL changing while both stock and native
link RX/TX remain at 5,770 MHz. However, 50 stock reads observe four measurement
frequencies, while 50 native reads observe 33 across its configured 42-channel
catalog. This is an unresolved active-catalog policy discrepancy, not proof of the
complete stock active set. The native window records 38 attributed power samples,
442 excluded events and 158 command-consistency failures. The large exclusion
count leaves interrupt timing and physical sample attribution unproven; changing
PLL shadows alone cannot close scan parity.

Startup, stop/start, deinit/init/start and candidate removal/readmission retain
compensation and the dark camera scene seen on fresh stock. The four direct
HDMI-to-USB captures total 70 seconds, with 82 linked/AP-time samples and zero
sampled socket drops. These video checks preserve the development baseline; they
do not resolve the scan catalog, sample-attribution or timing discrepancies above.

### Active catalog and Linux backend dependency

A subsequent read-only stock/native comparison establishes the active set for
this session. Stock getter `0x0100000a` returns the 42 configured frequencies;
getter `0x01000012` returns four enabled indices: **15, 23, 24 and 25**. These map
to 5,839, 5,740, 5,770 and 5,805 MHz, respectively. Three successive queries agree.
This confirms the earlier PLL observations for this session; it does not make
these four channels a fixed board default.

The matched original Linux backend explains an important dependency:

1. At initialization, it clears its cached catalog count, then requests
   `0x0100000a`. Only a successful reply populates its frequency catalog.
2. To select working channels, it matches desired frequencies against that
   cached catalog, subject to its validity, connection and relay policies.
3. If the resulting list is nonempty, it sends `0x02000023` with the selected
   channel indices. An empty result suppresses the setter entirely.

The backend's original instructions at `0x42bd24` (initialization) and
`0x42e68c` (work-list selection), checked against its matched binary, establish
these branches. Read-only process-memory observations corroborate their effects:

| Backend state | Stock firmware | Accepted Open8030 |
| --- | --- | --- |
| Cached firmware catalog | 42 frequencies | Empty |
| Desired work frequencies | 5,740 / 5,770 / 5,805 / 5,839 MHz | Same four frequencies |
| Selected work indices | 15 / 23 / 24 / 25 | Empty |
| Catalog getter result | Success | Unsupported (`-95`) |

Open8030 also rejects `0x01000012`; its host audit contains no `0x02000023`
request and has no recorded overflow. Absence from that audit therefore cannot
justify omitting the setter: an earlier unsupported getter prevents the normal
backend from reaching it. This is a concrete example of why observed native
traffic alone cannot define the original behavior inventory.

The next implementation must compare the channel-information getter, enabled-list
getter and work-list setter as a connected workflow. The setter also affects
channel selection and group state; matching only its enable-bit mask is
insufficient. Channel-information scan scores must retain their original meaning,
separate from Open8030's diagnostic sample counts. Getter error/null cases,
unused reply bytes, invalid/duplicate indices, band restrictions, current-channel
removal, lifecycle reset and backend startup all remain comparison requirements.
Original scan scoring and interrupt ordering remain separate open work.

This investigation changes no firmware bytes. Fresh 10-second stock and
20-second accepted-native HDMI-to-USB captures show the same dark textured scene;
all 23 native samples remain linked with advancing AP time and zero sampled
socket drops. The accepted image was retained in RAM. These observations do not
establish controlled freshness, image quality or measured latency.

### Channel-catalog command contracts

The independent [catalog module](../src/catalog.c) now implements serialization
and work-list decisions for this workflow. It is tested offline and **is not yet
connected to the firmware dispatcher or included in the firmware build**. All
three host commands remain unsupported in the running image. Runtime integration
must supply the original processed scan scores and implement the policy, AP
tuning and auxiliary-scan reset callbacks.

Original-instruction comparison covers 160 channel-information cases, 90
enabled-list cases and 1,395 work-list cases, including 3,086 ordered callback
actions. All 407 executed instruction locations are bound to the matched original
payload, container and analysis ELF bytes. Cases include both catalog banks,
empty through 128-entry catalogs, DEV/AP channel reporting, null enabled-list
output/catalog pointers, count boundaries, duplicate/invalid/forbidden indices,
automatic-policy precedence, AP state and callback-induced current-channel changes.

The original dispatcher, mode getter, base-channel score lookup and group/catalog
iterators execute. Memory copy, clock/division/logging, policy, AP lock/tune/unlock
and auxiliary-scan reset helpers are intercepted. The comparison validates their
arguments and order, not their internal effects. Processed scores are synthetic;
score generation and physical measurement attribution remain open. Original
unused reply bytes retain the seeded buffer contents; native serialization
deliberately zeroes them. Native bounds/null validation additionally rejects
unsafe structural inputs that are not executed against the original.

The work-list implementation preserves several consequential details:

- Invalid and forbidden entries do not enable channels, but the first supplied
  index can still become the fallback/AP tune request. The policy/tune callbacks
  must apply their own validation.
- Current-channel membership compares the complete 16-bit channel value;
  subsequent group lookup uses its low byte. Automatic policy takes precedence
  over current-channel membership.
- Group selector 3 matches any enabled channel. Other group winners are cleared
  only when their group has no enabled entries; surviving winners are retained.
- AP locking surrounds the conditional broadcast-channel update. The auxiliary
  scan reset follows group updates even when no channel change was requested.

The new native test suite passes with address/undefined-behavior sanitizers and
checks interoperability with the existing scan iterator. Fresh direct 20-second
HDMI capture of the unchanged accepted image retains the stock-matching dark
scene, with 23 linked/AP-time samples and zero sampled socket drops. This closes
the compared serialization and decision logic only; the connected backend
workflow, callback behavior, score processing and hardware parity remain open.

### Base-channel score processing

[Scan-score processing](../src/scan_scores.c) now follows the original DEV
base-channel handler. Each channel has ten raw samples and ten rolling means.
An admitted sample replaces the raw value at the shared ring index; the mean of
those ten raw values replaces the corresponding mean entry. The mean of the ten
means feeds the original integer score conversion. Both sums use 64-bit unsigned
arithmetic. The ring advances when the sampled channel has no later enabled
channel, and becomes ready after ten such rounds.

The helper also implements signed score ranking, stable ties, original 32-bit
wrap in band-score sums, group selector 3's wildcard and initial-band selection
with signed bias. A completed startup decision requests the original RX hold and
selection-message actions through its result; it does not execute those actions.
Native validation additionally prevents an unchecked invalid winner dereference.

Fresh original-instruction comparison covers 1,426 conversion inputs and 3,726
processing cases, including 1,512 completed rounds and 288 startup decisions.
It compares every channel's histories, latest sample and score, ring/flags,
band/group winners, band averages and selection effects. All 422 executed
instruction locations are checked against the matched payload/container/analysis
ELF bytes. Unsigned 64-bit division, logging/time, RX hold and selection-message
posting are intercepted. AP/subchannel processing, optional DEV reports and
physical sampling are outside this comparison.

The normal runtime feeds only events admitted by its existing tune/prepare/sample
guards into this processing. Pending, duplicate and unconfirmed events do not
change histories. It collects measurements without enabling automatic startup
selection or group policy. Rebuilding the native receiver context clears its
histories and readiness; complete original lifecycle retention still needs
comparison. The missing host-catalog commands remain unsupported.

Project diagnostic `0x015a0027` is now version 2, 392 bytes. It retains the original
53-word diagnostic prefix and appends ring index, readiness, completed-round count
and 42 signed scores. Zero denotes an unmeasured channel while its sample count is
zero; sample counts remain distinct from scores. These values use the original
arithmetic on a filtered subset of events. They are not proof of unbiased noise
measurements, equivalent sampling cadence or calibrated physical units.

Hardware records show all 42 configured channels receiving processed scores;
the startup snapshot has 973 attributed samples and 23 completed rounds. A
separate 50-read window records 41 attributed samples, 421 excluded power events
and 138 command-consistency failures. Stock still measures its four enabled
frequencies; native samples 29 distinct frequencies in that window. The active
catalog and large exclusion count remain unresolved, so score equality with
stock is not established.

All 25 native C suites and 18 Python tests pass. Startup, stop/start,
deinit/init/start and candidate removal/readmission preserve compensation and
the dark camera scene seen on freshly rebooted stock. Four direct HDMI-to-USB
captures total 90 seconds, with 102 linked/AP-time samples and zero sampled socket
drops. Both lifecycle rebuilds clear native score histories/readiness, which
subsequently advance again. The candidate was retained in RAM after inspection.
This does not establish controlled scene freshness, measured latency or full
scan/selection parity.

### Work-list helper contracts

[Channel-work helpers](../src/channel_work.c) implement three dependencies of
the catalog work-list setter. They are source-only and are not yet included in
the firmware build or host dispatcher.

- Backup selection ranks up to four enabled channels in a requested group,
  using the **signed low 16 bits** of each processed score. Equal scores retain
  catalog order. Group 3 is a wildcard. When nothing qualifies, the original
  returns the current channel, even if it is disabled or in another group.
- A channel request updates pending state before posting destination 1/message
  `0x108`. Selector 1 records a 16-bit pending channel; other selectors record
  its low byte as the broadcast channel. Posting success sets an additional
  flag; failure preserves the pending state. This stages a request, not an
  immediate retune or proof of a completed channel change. The native payload
  zeroes two padding bytes that the original leaves unwritten.
- Auxiliary-scan reset clears six group counters and 128 channel counters,
  writes RF field `0x08080f01 = 1` **twice**, then resets six winners, the selected
  group marker, a state flag, 128 opaque 14-byte records and 128 marks. This is
  separate from the ten-sample noise-score histories. The record producers and
  consumers have not been fully compared; their byte layout is not a decoded
  protocol declaration.

Fresh original-instruction comparison covers 1,440 ranking cases, 1,084 request
cases and eight reset cases, including repeated reset. It checks both catalog
banks, empty eligible sets, count boundaries through 128, signed truncation,
stable ties, all 256 request-flag values, selector/channel boundaries, successful
and failed queue posts, and state visible during both reset writes. All 267
executed instruction locations are bound to the matched original payload,
container and analysis ELF bytes. Reset comparison also checks the surrounding
original bytes remain unchanged.

Memory fill/move, time/division/logging, queue posting and RF field writes are
intercepted. The comparison validates the queue arguments and ordered field
operations, not the queue consumer, register writer or physical channel change.
Native validation additionally rejects unsafe pointers, counts and current
indices before mutation. The dedicated sanitizer suite passes. A fresh
20-second direct HDMI-to-USB capture of the unchanged accepted image shows the
stock-matching dark scene, with 23 linked/AP-time samples and zero sampled socket
drops. No fresh stock capture or controlled scene-freshness test was performed
for this source-only iteration.

The complete policy wrapper, hopping/backup-message exchange, queued channel-change
consumer and runtime ownership of auxiliary state remain open. In particular,
these helpers do not make the catalog getter/work-list setter workflow supported;
all three host commands remain marked unsupported in the command inventory.

### Selector-1 channel policy

[The policy helper](../src/channel_policy.c) implements selector 1, used by the
work-list setter, including its auxiliary hopping-mode decisions. It remains
source-only. The caller supplies the current base-channel catalog, processed
scores, transmit channel and state; the helper does not infer them from a fixed
frequency or treat a queued request as a completed tune.

Manual selection updates mode flags and stages a request when the full 32-bit
requested value differs from the current 16-bit channel. The request payload is
then truncated to 16 bits. Queue failure is deliberately not returned as policy
failure: the original returns success while preserving pending request state.

With auxiliary hopping enabled, requesting mode 2 refreshes four ranked backup
entries. Each selected channel has bit 8 set; unused entries are `0x00ff`. DEV
sends internal command 10 to slot 0; AP sends to each enabled slot. A 3,000 ms
timer is armed after the sends, then the auxiliary pending flag is set. Send
errors do not suppress the timer, and an AP with no enabled slots still arms it.
Entering hopping from inactive state can instead send a PHY request containing
the current transmit channel, depending on the auxiliary flags.

Leaving hopping sends a PHY request using the requested low channel byte, or the
auxiliary channel if no explicit request was supplied. On successful enqueue,
the local channel/band/group are updated from the auxiliary channel, even when
the requested channel differs. Ordinary enqueue errors are returned without
those state changes. The special result `-2` falls through to manual policy.
This establishes enqueue-side decisions only; peer acknowledgement and physical
channel completion remain separate work.

Fresh comparison executes the original selector-1 wrapper, auxiliary policy,
ranking, request staging, band/group lookup and backup broadcast helpers. Across
3,440 cases it compares 601 executed instruction locations, 1,275 queue posts,
552 PHY requests, 1,424 internal-command sends, 564 timer arms and paired nested
lock/unlock effects. Cases cover all 256 auxiliary/request flag values, missing
arguments, mode/channel boundaries, both catalog banks, counts through 128,
empty eligible groups, both roles, callback-time mode changes and failed sends.
Every executed instruction is checked against the matched original payload,
container and analysis ELF bytes.

Memory utilities, logging/time, locks, queue/PHY/control sends and timer arming
are intercepted. Their arguments, order and visible state are compared; lock
contention, actual transmission, timer expiry and queue consumers are not. The
native helper clears an original unwritten PHY high nibble and request padding.
Native bounds validation excludes unsafe storage/current indices. Other policy
selectors and current subchannel states are not covered by this helper.

The policy sanitizer suite and both dependency suites pass. A fresh 20-second
HDMI-to-USB capture of the unchanged retained image again shows the dark textured
camera scene, with 23 linked/AP-time samples and zero sampled socket drops. This
rechecks baseline video, not hardware integration of the new policy. The three
catalog host commands remain unsupported pending queued channel transitions,
runtime integration and normal backend/hardware comparison.

### Channel request and completion contracts

[Transaction helpers](../src/channel_transaction.c) implement the original
link-worker request message `0x108` and PHY-worker completion message `0x109`.
These are internal worker messages, not host API IDs or internal port-0 command
numbers. The helpers remain outside the firmware build and dispatcher.

The request handler locks the role's link state and enqueues PHY command 4 with
TTL 16 and a three-byte payload. DEV encodes the slot's low nibble and requested
channel. AP encoding additionally depends on selector, mode, channel flags and
relay configuration. Already-selected DEV requests and selector-0 AP broadcast
requests unlock without sending. Any enqueue error produces a failed completion
message after unlocking, preserving the original selector and requested channel.
Failure to post that completion does not synthesize a successful channel change.

The completion handler locks policy state. Selector 1 clears the inflight flag,
both bytes of its history marker and two lists of counter bytes when that flag
was set. Successful completion commits only if the **full 16-bit channel**
matches the pending value; band/group lookup then uses its low byte. A successful
mismatch still performs the inflight/history reset but preserves current and
pending channel state. Failure with error zero stages a rollback request to the
current channel. A failed rollback post leaves that request pending; success
sets its inflight flag again. Nonzero-error failures do not post rollback.

Other selectors use the broadcast inflight/pending flags. They reset the two
counter lists without clearing the separate history marker, and only a matching
successful channel clears the broadcast pending flag. The full returned word is
compared with the stored broadcast byte; matching low bytes alone is insufficient.

Fresh comparison covers 4,330 request cases and 9,312 completion cases. It checks
both roles, AP mode/relay combinations, slot/channel/selector boundaries, all 256
policy flag values, successful and failed sends/posts, matching and stale
completions, null/partial/full counter lists and neighboring object bytes.
The original request/completion dispatchers, counter-list reset, catalog lookup
and rollback staging execute. All 397 executed instruction locations are checked
against the matched payload, container and analysis ELF bytes. Ordered effects
include 3,790 PHY enqueues, 3,305 queue posts and 13,642 paired lock/unlock calls.

Locks, PHY enqueue, queue posting and time/logging are intercepted. Their state,
ordering and arguments are compared; transmission, scheduling and physical
acknowledgement are not. Native code clears original unwritten completion/request
padding and rejects invalid matched-channel storage before mutation. The policy
callback's argument names were also corrected to **command/TTL**: its existing
`8, 8` arguments mean PHY command 8 with TTL 8, not a user and port. Behavior and
previous comparison values are unchanged.

Transaction, work-helper and policy sanitizer suites pass. A fresh 20-second
HDMI-to-USB capture of the unchanged retained image shows the same stock-reference
dark scene, with 23 linked/AP-time samples and zero sampled socket drops. No
fresh stock capture or controlled freshness/latency measurement was performed.
The PHY command-4 callback, actual tuning and monitor-driven completion remain
open. Runtime integration and the normal backend work-list workflow still need
hardware comparison; the three host catalog commands remain unsupported.

### PHY channel-change callback

[The channel-link helper](../src/channel_link.c) implements the original PHY
command-4 callback for AP and DEV modes 0–3, including the cross-band monitor
preparation helper. It remains source-only. Its tuning, notification, queue and
register callbacks are comparison boundaries, not implemented hardware effects.

Channel changes follow role, mode, callback direction and payload flags. State
updates precede their corresponding tune requests. Notifications retain the
original user/direction and old/new low channel bytes, including overlapping
flags that can report the same old and new value. Tune and notification failures
do not undo the original state updates.

In mode 0, an outgoing DEV request for its own slot arms the user-8 monitor; an
outgoing AP receive-channel request arms user 0's monitor. A band change first
sets the monitor's preparation flag and hold-mask bit and writes that user's RF
field. The callback then marks completion pending, stores the channel's low byte
and sets the monitor deadline to its threshold plus 16, with byte wrap.

The auxiliary-search path runs after the role's link lock is released, even when
the role-specific request was ignored. An enabled auxiliary context and payload
bit `0x20` select subchannel 1 of the requested base channel and issue its tune
under the auxiliary lock. AP suppresses incoming changes for any nonzero
ignore-once value; DEV suppresses only positive values. This difference and the
common auxiliary path are preserved.

Outgoing callbacks post an immediate successful worker completion only when the
monitor's pending bit is clear. Its selector follows the original role/mode/flag
rules. This can occur after ignored role-specific work and does not by itself
prove a physical retune. The native payload clears the original unwritten
padding/error bytes. Pending monitor completion must be produced by the monitor
path, which remains unintegrated.

Fresh comparison covers 5,728 cases: 3,668 handled requests and 2,060 invalid
length/header cases that the original silently ignores and native code rejects
with `-22`. Cases cover all payload flag bytes, both roles, modes 0–3, assigned
and unassigned slots, pending monitors, ignore-once signs, auxiliary search,
channel boundaries, matching/different bands and monitor-deadline wrap. The
original callback, cross-band preparation, band lookup and hold-mask writer
execute. All 593 executed instruction locations are checked against the matched
original payload/container/analysis ELF bytes. Comparison includes untouched
link-object bytes and state visible at every intercepted operation.

Ordered effects include 7,570 tune calls, 188 RF field writes, 1,028 notifications,
806 completion posts and 7,336 paired lock/unlock calls. Actual tuning, register
access, notification delivery, worker scheduling and time/logging are intercepted;
errors returned by tune/notify/post callbacks are intentionally exercised.
Unsafe cross-band catalog accesses are excluded and rejected before effects by
native validation. This does not establish RF channel-change timing or parity on
other physical boards/roles.

The dedicated sanitizer suite passes. A fresh 20-second direct HDMI-to-USB capture
rechecks the unchanged accepted image: the same stock-reference dark camera
scene, 23 linked/AP-time samples and zero sampled socket drops. No fresh stock or
controlled freshness/latency test was performed. Channel resolution/MMIO tuning,
PHY command transport, monitor completion and runtime integration remain open;
the normal backend catalog/work-list workflow is still unsupported.

### Channel resolution and generic tuning

[The tuning helper](../src/tune.c) implements channel-to-frequency resolution and
per-user TX/RX tuning. The runtime now uses it for measurement RX user 11 on the
existing single-band sweep. Other users, channel-change callbacks, subchannels
and TX power effects remain unintegrated.

The resolver uses the low channel byte as the catalog index and all remaining
bits as the subchannel number. It masks catalog frequencies to 24 bits, returns
zero for an invalid index and applies the configured offset and subchannel
spacing only for a nonzero subchannel within the configured count. Bandwidth
codes 0–5 select `1250 << code` kHz; other codes use zero width. Arithmetic retains
the original unsigned wrap. The separate subchannel-bandwidth setting does not
participate in this calculation.

Tuning writes five PLL bytes in order, then the user's RF band/path fields.
TX optionally dispatches the dual-band offset and refreshes power under its
mutex when the stored signed-byte offset differs from the requested integer.
The offset is narrowed before the power callback, whose failure does not undo
the state change. RX users below 11 receive frequency and gain-hint fields;
user 10 always uses the default hint. Gain rows keep their supplied order and
select the first threshold strictly above the frequency, or the final row.
Bank and frequency metadata updates follow these effects.

Fresh comparison covers **11,184 tuning cases and 2,328 resolution cases**.
It executes the original tuner, PLL field mapper, serial byte writer, path
selectors, TX band dispatcher, channel resolver and subchannel configuration
lookup. All 401 executed instruction locations and the bandwidth table are
checked against the matched original payload/container/analysis ELF bytes.
Cases cover all twelve users and both directions, band boundaries, noncanonical
boolean values, gain-table ordering, subchannel bounds, invalid indices, signed
offset truncation, integer wrap and power-refresh errors. Native structural
validation rejects unsafe storage, users and dimensions before effects.

Comparison includes 55,920 byte-write arguments, 43,452 field writes, 22,368
metadata updates, 3,160 band-offset dispatches, 1,176 power calls and 2,352 paired
lock/unlock calls. These are compared arguments, ordering and state, not physical
hardware operations. PLL arithmetic is replaced by a supplied synthetic 40-bit
word; the existing runtime PLL calculation is unchanged. Register access,
band-offset application, power refresh, metadata internals, mutexes and time/log
helpers are intercepted. Frequencies outside physical operating ranges exercise
arithmetic only and do not establish support for those ranges.

All 30 native C suites and 18 Python tests pass. The standard build reproduces
the candidate exactly. Hardware command registers match three stock windows;
the measurement PLL sweeps while link RX/TX remain at 5770 MHz. Startup samples
cover all 42 configured channels and reach score readiness. Stop/start,
deinit/init/start and candidate removal/readmission preserve TX compensation.
The final four HDMI-to-USB captures total 90 seconds, with 102 linked/AP-time
samples and zero sampled socket drops. Inspected frames show the dark textured
scene from the initial fresh-stock reference. The candidate remains in RAM.

The first test window expired after successful startup/restart captures. The
repeated sequence began with another receiver reboot. That stock capture showed
the fallback background despite a link; loading the native image restored the
camera scene. This is retained as a startup-reliability limitation, not counted
as successful stock video or attributed to the new tuner. There was no controlled
freshness, bright-scene quality or latency measurement. Native still enables 42
channels versus stock's four and excludes ambiguous polling events. Active
catalog, physical sample attribution, PHY transport, monitor completion and
runtime channel policy remain open; the three host catalog commands are still
unsupported.

### Channel-change monitor completion and hopping

[The monitor helper](../src/channel_monitor.c) implements the original observation
and periodic channel-monitor state transitions. Normal mode-0 loss monitoring
now uses it as described below. Channel-change callback/completion and hopping
worker integration remain open.

The ten users retain enabled/locked, forced-loss, two-bit acquisition streak,
band-hold and pending-completion flags. Success and automatic-unlock counters
are shared across users. An unlocked observation of a user that has not acquired
lock resets its streak and requests an RX reset immediately. Successful
observations advance the streak with two-bit wrap. Gaining lock clears the
pending-completion bit as well as the streak. This is distinct from observing
an already locked user with a pending channel change.

A successful observation releases a pending band hold before counting completion
successes. The third counted success clears the pending bit and posts worker
message `0x109` with success 1, selector 1 and the low channel byte. Byte wrap and
counter resets are preserved. Queue-post failure does not restore pending state.
The periodic path increments each enabled user's missed count with byte wrap.
A still-locked pending user posts failure when that count equals its deadline.
Forced loss or reaching the saved threshold invokes the optional loss callback;
cleanup then clears forced/locked/pending flags and the missed count, while
preserving other callback changes. Failure posting precedes the loss callback
when both conditions occur in one tick.

Periodic RX resets occur every ten misses after 19 for AP or 50 for DEV, subject
to the original reset-suppression condition. Other role values do not enter that
reset path. Mode 0 resets globally; other modes reset the selected user. These
callbacks and the loss callback are caller-owned operations, not hardware
implementations in this module.

Automatic policy clears the shared unlock counter on a successful observation.
Otherwise, after ten counted unlocked observations and without a pending request,
it queries the current and preferred channels. A different preferred channel
can trigger a hop immediately. If there is no alternative, more than forty
observations permit the original group-iteration and endpoint fallback logic.
The fallback retains signed comparison of wrapped 32-bit distances and narrows
the chosen endpoint to a byte. A hop records the band's timestamp and clears the
unlock counter before invoking its execution callback. Channel queries, group
iterators and actual hopping remain comparison boundaries.

Fresh original-instruction comparison passes **5,784 observation cases and 7,200
periodic cases**. It covers every flag byte, all ten user positions, multiple
enabled users, counter/deadline wrap, both roles and other role values, modes,
suppression, null/mutating loss callbacks, band holds, completion-post errors,
preferred-channel boundaries and blind-hop decisions. All 567 executed
instruction locations are checked against the matched payload, container and
analysis ELF bytes. Comparison includes complete monitor state at each effect:
941 reset requests, 934 field writes, 1,665 completion posts, 2,577 loss callbacks,
292 current-channel queries, 292 preferred-channel queries, 309 iterator calls
and 252 hop requests.

The original periodic handler's diagnostic reads use synthetic inputs; its
diagnostic history writes are excluded from the native state comparison. The
clock supplies a fixed millisecond value. Timer scheduling, diagnostic history,
physical resets/retuning, queue delivery and selection-helper internals are not
established by these tests. Original unwritten completion padding is excluded;
the native helper clears it. Synthetic layouts and ABI remain analysis metadata.

The dedicated address/undefined-behavior sanitizer suite passes. A fresh
20-second HDMI-to-USB capture rechecks the unchanged accepted image, with the
reference dark textured scene, 23 linked/AP-time samples and zero sampled socket
drops. It is not hardware validation of the new monitor. No new stock capture or
controlled freshness/latency test was performed. PHY transport, worker and
policy integration, diagnostic history and physical channel-change timing remain
open; the three host catalog commands remain unsupported.

### Pending PHY command transport

[The PHY control helper](../src/phy_control.c) implements the original single-slot
enqueue, periodic transmitter, cancellation and receive-timer callback. Its receive-timer entry and normal mode-0 MCS sender use are now integrated
as described below. Broader command callback effects
in `rf_device.c` remain restricted.

Enqueue locks the control state and returns `-1` when its command nibble is busy.
An accepted request preserves the header's high status nibble and unused payload
tail, writes command/TTL/length and optionally sets the wake field. It does not
immediately transmit. Native validation returns `-22` before effects for unsafe
storage, commands above 9, TTL above 31 or payloads over four bytes; the original
enqueue has no equivalent bounds checks.

The transmitter sends each nonzero TTL before decrementing it. On a subsequent
tick at zero, it invokes the outgoing callback, clears the pending command and
wake field, and writes the idle packet. That zero-TTL branch retains the existing
checksum unless the AP mode-3 idle-refresh callback replaces it. Normal countdown,
TTL-31 retransmission, command-7 clearing and explicit cancellation recompute the
checksum. TTL 31 remains pending until cancellation; command 7 clears on its next
tick without a local command callback. The tick counter wraps as a byte even
when no packet is emitted.

An idle AP can emit time synchronization after more than 60,000 milliseconds,
or when its validity flag requests it. The first broadcast derives its cached
lead byte from the wrapping frame-duration calculation. The timestamp uses the
low 32 clock bits plus that lead and carries its high nibble in the header.
Checksum generation processes the six bytes after the checksum, forcing the
last byte's high nibble to `0xf`. Mode 3 preserves the checksum byte's high nibble.

The receive timer dispatches the currently cached command when its TTL is
nonzero, its command is below 10 and a callback is registered. It does not clear
the cache or prove that the command which armed the timer is still cached.
Callback bodies run under the control lock. This helper compares dispatch at
that boundary; receive validation, cache replacement and timer arming are still
separate work, including mode-3 receive side effects.

Fresh comparison covers **6,400 enqueue, 6,200 transmitter, 192 cancellation and
5,120 receive-timer cases**. All 363 executed instruction locations and all 256
CRC table entries are bound to the matched original payload/container/analysis
ELF bytes. Cases cover busy/empty slots, all five-bit TTL values, payload lengths,
mode-3 checksum preservation, absent/mutating callbacks, invalid timer command
indices, time thresholds and 32/64-bit wrap. Complete state and ordered effects
match: 17,912 paired lock/unlock calls, 3,128 field writes, 6,004 packet writes,
1,614 callback dispatches and 32 AP idle refreshes.

Locks, memory copying, MMIO output, callback bodies, AP mode-3 idle construction
and clock/logging helpers are intercepted. Every clock call receives the same
fixed millisecond fixture; elapsed time between calls is not compared. Synthetic
callback registrations and layouts are analysis inputs, not original firmware
declarations. Packet-write arguments do not establish physical transmission,
receiver acknowledgement or runtime scheduling.

The dedicated sanitizer suite passes. A fresh 20-second HDMI-to-USB capture
rechecks the unchanged accepted image: the reference dark camera scene, 23
linked/AP-time samples and zero sampled socket drops. The accepted image remains
in RAM. No fresh stock capture or controlled freshness/latency measurement was
performed. Broader channel-policy traffic still needs callback and worker integration,
plus runtime validation beyond the MCS producer described below.

### PHY control receive and cache replacement

[The receive helper](../src/phy_receive.c) now compares the original seven-byte
read, checksum/length validation, shared cache, mode-3 side effects, command-0
admission construction and timer decisions. The initial comparison was
source-only. Normal mode-0 runtime integration is described below; broader
command effects and mode-3 behavior remain open.

Read failure or invalid format leaves the previous cache intact. Mode 3 checks
only the checksum's low nibble. For DEV role 1 it prepares retransmission state
when needed, consumes the slot's high-nibble bit and updates the disable flag
and corresponding field from byte 2. Other roles skip these DEV effects. An
unconnected DEV with matching MAC XOR and position can synthesize a 150-byte
admission record from command 0. The record is compared byte for byte; admission
itself remains a callback boundary, including callback mutations and errors.

Command 7 replaces the AP-time offset without cancelling an earlier timer.
Other nonzero commands dispatch at TTL 31, stop the timer and dispatch at TTL 1,
or arm it at TTL 2–30 using wrapping 32-bit frame-duration arithmetic. TTL 0
does nothing further. Dispatch re-reads the cache after the stop callback, so a
callback's replacement message is retained. Command 0 accepts every declared
length; in mode 3, admission can use that record even when its length is not 3.

Fresh original execution matches **26,120 receive cases** and **6,455 subsequent
expiry sequences**. Cases cover command/length/TTL boundaries, checksum failures,
mode/role differences, every retransmission phase byte, slot masks, admission
matching, clock/frame wrap, read failures and callback mutation. Complete state
and ordered effects match, including 1,262 field writes, 2,680 timer stops,
1,680 timer starts, 2,734 dispatches, 1,280 preparations and 126 admission calls.
The complete DEV and retransmission objects are checked for collateral writes.
All 371 executed instruction locations, descriptor lengths, callback presence
and CRC table bytes are bound to the matched original payload/container/analysis
ELF. Original callbacks are absent for commands 0, 7 and 9.

The expiry sequences exposed an overly broad native storage guard: a valid
command-0 cache can declare more than four bytes, yet expiry must be a no-op
when no callback is registered. The guard now checks dispatchability under the
control lock before rejecting an unsafe callback length. Both PHY sanitizer
suites pass, and replay of the prior 17,912 original transport traces passes.

Locks, physical reads/field writes, allocation, timers, admission, dispatch and
clock/logging boundaries are intercepted. Fixed clocks do not prove elapsed
timing. Synthetic layouts and ABI are analysis metadata. This establishes helper
decisions, not actual admission, RF transmission or timer/IRQ integration.
A fresh 20-second HDMI-to-USB capture checks the unchanged accepted image:
the reference dark textured scene, 23 linked/AP-time samples and zero sampled
socket drops. No new stock capture or controlled freshness/latency test was
performed. Physical callback effects and runtime integration remain open.

### Runtime PHY receive integration

The normal mode-0 receiver now calls the compared receive and expiry helpers.
This replaces duplicated length validation, shared-cache scheduling and AP-time
storage. The physical header extraction still uses the existing register-bank
reader. The original callback-presence mask prevents command 9 from being
incorrectly counted as an unsupported dispatch at immediate or timer expiry.
A long declared command-0 length can replace an armed cache without triggering
an unsafe dispatch. Stop/start retains the AP-time offset; deinit/init clears
validity until a fresh time message arrives.

The runtime remains a serialized polling loop with interrupt delivery masked.
The helper lock callbacks therefore have no runtime locking work; a future
thread/interrupt port must implement synchronization. Runtime configuration
still supports mode 0 and local position 0 only. Synthesized command-0 records
request mode-3 admission, which is explicitly rejected as unsupported rather
than interpreted as a wire MAC message. Mode-3 retransmission state, actual
admission/reconfiguration, sender integration and the unimplemented command
callbacks remain open.

Replay of the prior original traces passes all 26,120 receive cases and 6,455
receive-then-expire sequences. This reuses the recorded original execution;
it is not a new execution of the original oracle. RF peripheral integration,
PHY receive and PHY transport sanitizer suites pass, including new command-9
and long-command-0 cache regressions. Packaging/tool tests pass separately.

Hardware startup, stop/start and stop/deinit/init/start captures total 60 seconds,
with 69 linked/AP-time samples and zero sampled socket drops. The dark textured
camera scene matches a fresh 10-second stock HDMI-to-USB reference. Calibration
registers remain stable across both lifecycle sequences, and deinit/init clears
AP-time validity before fresh reception restores it. The new image is retained
in RAM. This does not establish controlled freshness, latency, mode-3 operation
or hardware execution of every command/cache boundary.

### Shared PHY sender integration

Normal mode-0 MCS requests now use the compared pending-command transport.
Before a policy decision, the MCS helper receives a snapshot of the shared TX
slot. Its enqueue/cancel intent is then applied through the transport helper,
which owns the command bytes, checksum, wake field and packet writes. The MCS
snapshot is refreshed after intent application. There is no independent pending
MCS packet competing with the shared slot.

Enqueue requests command 3 with TTL 31 and a two-byte slot/MCS payload. It wakes
synchronization without writing the FCH registers. Source-18 interleaver handling
calls the transport tick; its packet callback writes the zero-padded seven-byte
control packet to B8C/B90. A matching MCS observation or retry-phase cancellation
clears the shared command through the compared cancel routine, preserving TTL,
length and payload tail while recomputing the checksum. Configuration rebuild
resets TX/RX caches while preserving the separately scoped AP-time lifetime.

Replay of the earlier original traces passes 6,400 enqueue, 6,200 tick, 192
cancellation and 5,120 receive-timer cases. It reuses recorded original execution.
RF integration, MCS policy and PHY transport sanitizer suites pass. The RF suite
checks that preencode enqueue precedes the first register write, validates both
written words, and verifies acknowledgement-driven cancellation and wake clearing.

Hardware startup, stop/start and stop/deinit/init/start captures total 60 seconds,
with 69 linked/AP-time samples and zero sampled socket drops. All inspected
frames show the dark textured scene from a fresh stock HDMI-to-USB reference.
Each candidate session records six MCS requests/acknowledgements, five
advertisements/cancellations and 25 control-register writes, with no reset
errors. Calibration survives both lifecycle sequences. The image is retained in
RAM; controlled freshness, latency and multi-producer behavior remain unverified.

The runtime's active sender producer remains the MCS policy. Channel workers,
finite-TTL outgoing command callbacks, AP time broadcasts and mode-3 idle packet
construction still need integration and hardware validation. Timer/IRQ cadence,
RF acknowledgement timing and competition between actual policy producers are
not established by the isolated helper comparisons.

### Runtime channel monitor integration

Normal mode-0 user-8 observation and periodic loss handling now use the compared
channel monitor. The runtime keeps its established threshold/interval calculation,
enables the locked monitor on admission, disables it on stop and rebuilds it on
start. Recovery diagnostics read the authoritative monitor flags and counters.
The periodic adapter advances once per elapsed interval; delayed polls do not
manufacture catch-up ticks.

Reset callbacks set the existing deferred-reset request. The radio waits until
an owned DMA view is released before resetting RX. A loss callback stages the
existing host recovery path; candidate removal still stages immediate recovery
and forces the monitor when enabled/locked. This preserves native bounded waits
and ownership handling, rather than reproducing unbounded original waits.

The RF recovery suite, complete monitor suite and threshold/timing suite pass
with address/undefined-behavior sanitizers. Replay of the recorded original
execution passes 5,784 observation and 7,200 periodic cases with 567 bound
instruction locations. No new original oracle execution was performed for this
integration. The RF suite exercises loss, unrelated-user filtering, delayed DMA,
candidate removal/readmission, changed-peer-token recovery and timeout failure.

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

Only normal mode-0 logical user 8 is enabled. Channel-policy hopping is not
enabled, and no producer arms the channel-completion flag. Completion queue
delivery remains explicitly unsupported until its worker is connected. Other
users/modes, original diagnostic history, physical peer silence and precise
IRQ/timer timing remain open. The compared monitor's wider behavior alone does
not establish those runtime capabilities.

### Tuning power callbacks

[The power-control helpers](../src/power_control.c) implement the per-user
calibration application and band-offset field writes called by the original
TX tuner. The initial comparison was offline; initial loader user-0 application
is now integrated as described below. Dynamic tuning and band-offset callbacks
remain pending. The existing pure calibration planner supplies ordered writes.

Power application preserves an original cache alias: the shared current-level
byte is also user 0's cached level. Open-loop field writes occur first, then
that shared byte changes, then closed-loop writes occur, then the requested
user's cached level changes. Applying user 8 or 11 therefore also changes the
cached level for user 0. This is a software cache effect, not evidence that both
users' hardware power changed. Caller serialization is required; the helper
itself does not acquire the original mutex.

Calibration uses independently clamped signed path-plus-user offsets for closed
paths, unadjusted analog rows for open paths, clock-dependent amplitude scaling
and the DEV user-8 CSMA gain reduction with zero amplitude. Invalid byte-sized
levels return the original `-3`. Native validation additionally rejects unsafe
users, missing calibration, invalid configuration or absent callbacks before any
state change or field write. Wider original argument-wrap aliases are outside
the native contract.

Band-offset handling accepts signed byte offsets. Values outside -6 through +6
have no effects. With both paths closed, it selects the original paired gain
steps; otherwise the first nibble is 12 and the second is 12 plus the offset
clamped to -3 through +3. The first field precedes the second. Users 0–9 use
paired per-user fields, user 10 uses dedicated fields, and user 11 has no fields.
This operation changes neither calibration offsets nor cached levels.

Fresh execution of the matched original instructions passes **50,772 power
cases and 12,288 band-offset cases**, including every byte-sized power request
for all 12 users, four closed/open combinations, both clock choices and both
roles. Of the power cases, 7,572 apply valid levels and 43,200 reject invalid
byte-sized levels. Additional cases exercise signed offset extremes, calibration
clamps and CSMA reduction boundaries. Band comparison covers every signed byte offset for
all users and closed/open combinations. It checks 24,336 power field writes and
1,144 band field writes, their order, cached state at each write, final state and
collateral memory preservation. The power and power-control sanitizer suites
also pass, including atomic native rejection cases.

The comparison executes the original power application at `0x24515a`, band
policy at `0x228ec6` and field-address helpers at `0x21db68`/`0x21db9c`.
All 234 executed instruction locations and the policy lookup bytes are bound to
the hash-matched payload, container and synthetic analysis ELF. Only field MMIO
and the 13-byte memory copy are intercepted. Calibration/state inputs are
synthetic; native state structures are independent of the recovered memory layout.
No emitted-power measurement, valid factory-record comparison, dynamic host
setter, channel-switch integration or low-power acquisition fix is established.

In that source-only iteration, the rebuilt firmware remained byte-for-byte
identical to the retained stage-187 image. A 30-second direct `/dev/video0`
HDMI-to-USB capture showed the same dark textured camera scene and camera OSD;
all 33 samples remained linked with advancing AP time and zero sampled socket
drops. Boot identity and image, configuration, backend and daemon hashes matched.
That regression check did not validate the new helpers on hardware. No new stock
capture or lifecycle transition was performed in that source-only iteration.

### Runtime calibration and initial power application

The normal loader now passes resolved calibration into the RF runtime, including
available rows, path offsets, clock/role settings and CSMA adjustment. Initial
user-0 power is applied through the compared power-control helper, replacing the
four direct gain-field writes at the same point in receiver preparation. The
runtime retains its own calibration copy and the cached user-0 level. Other
users' cached levels remain uninitialized; they are not inferred from register
values or copied from user 0.

Before RF writes, the runtime checks that the supplied calibration is valid for
the requested initial level, uses the supported DEV/high-clock/closed-loop
profile, and reproduces the DEV4 plan's four gain values. Missing rows, absent
calibration or disagreement with the plan reject preparation. Saved host
configuration retains its own calibration copy for stop/start, deinit/init and
candidate/loss recovery. Bounded diagnostic plans carry only precomputed gains;
they clear the current power context and cannot inherit calibration accidentally.
A later normal preparation restores calibration from saved configuration.

Boot and RF integration tests verify the handoff, caller-storage independence,
initial register fields, rejection without RF writes, lifecycle/recovery
retention and diagnostic isolation. These pass alongside the power planner and
power-control sanitizer suites. Replay of the recorded original power traces
still matches all 63,060 cases; this integration does not execute a new original
oracle run. It establishes initial application only. Dynamic host setters,
channel-tuning callbacks, other users' initialization, band-offset effects and
the low-power acquisition failure remain open.

The connected-state gain comparison exposes an existing policy gap. Stock user 0
has gain/amplitude pairs `(34,39)` on both paths, matching calibration row 14.
Open8030 has `(23,78)` and `(22,78)`, matching its configured initial row 25.
The backend audit records request `0x02000008` with payload `00 0e` (user 0,
level 14), which remains unsupported. This is consistent with the missing host
setter leaving Open8030 at its startup level; implementing and testing the full
setter/policy path is required to establish the cause and close parity. A
hash-verified accepted stage-167 register capture has the same native values,
so this difference predates the new power-helper integration. These register
values are not calibrated emitted-power measurements.

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

### Manual power commands: compared helpers, failed integration

Source-only `power_host.c` models mode getter `0x01000007`, cached level getter
`0x01000008` and direct setter `0x02000008`. Replies are respectively one byte,
two bytes (user and level), and empty. Invalid getter users return level `0xff`.
The setter locks, applies calibration and cache updates, then unlocks; invalid
levels return `-3`. Native malformed requests and unsafe setter users return
`-22` before effects. These commands remain unsupported in the runtime.

Fresh original-instruction comparison covers **12,550 host-command scenarios**
and **4,096 all-user initialization scenarios**. Setters cover all byte-sized
levels for each valid user and all four closed/open path combinations, with
varied clocks and roles. Each setter is followed by original/native getters for
user 0 and the addressed user. Getter cases cover every byte-sized user and mode
truncation boundaries. Comparison checks 4,464 manual field writes and 17,856
initialization writes, lock/effect order, output bytes, status, cache snapshots,
final state and collateral memory preservation.

The source-only all-user initializer preflights every user's calibration before
effects. The original loop returns its mutex-release status even after a failed
application; native initialization returns the validation error. The comparison
records 3,600 such deliberate differences. It does not count these as original
error parity. Sanitizer tests cover framing, aliasing, lock/error ordering and
atomic rejection when the last user's required calibration row is absent.

The original core dispatcher, getter bodies, setter at `0x246b3e`, mutex wrapper
at `0x228f58`, calibration application and all-user loop at `0x248ffc` execute.
All 308 executed instruction locations and relevant routing tables are checked
against the matched payload, container and analysis ELF. Field MMIO, mutex
internals and diagnostic time/logging are intercepted; calibration and object
state are synthetic. This does not cover transport framing, malformed original
pointers, unsafe original setter users, concurrency or emitted RF power.

Fresh stock hardware checks confirm the mode/getter replies, levels 25 and 14,
four invalid levels, and the reporting-cache alias: setting user 5 changes user
0's reported level without changing user 0's gain fields. Stock is restored to
level 14 and its camera scene is inspected in a 10-second HDMI-to-USB capture.

Stage 190 integrated these commands and all-user initialization experimentally.
The normal backend selected level 11 before acquisition; user-0 fields read
`(37,30)` on both paths. Acquisition failed, all 33 samples in a 30-second capture
were disconnected/searching, and direct frame inspection showed the disconnected
UI. The candidate was rejected. The low-power failure is also reproduced by the
earlier configuration-only test; this experiment does not isolate its cause.

A full receiver reboot and reload restored accepted stage 189. Its 30-second
capture has 33 linked samples, advancing AP time and zero sampled socket drops;
inspected frames restore the dark textured camera scene and OSD. It is retained
in RAM. Runtime integration was removed, and the standard build again produces
the accepted runtime and container hashes exactly. Only the compared helpers,
tests and coverage records are retained from this trial. Private stage-190
evidence includes the failed source/image, comparison receipts, stock checks,
device settings, captures and rollback provenance. Controlled scene freshness,
latency and physical transmit power remain unmeasured.

### Interleaver service and low-power investigation

The source-only `interleaver.c` helper models the original service at `0x227d58`:
conditional distance-clock correction, B4C publication, request-ID extraction,
per-user drain/retry bookkeeping, MCS-application and reset callbacks, queue
message `0x202`, then observer notification. The first hardware user identifies
the published request; a separately sampled second user selects the bookkeeping
entry. Treating them as necessarily identical would lose an original read boundary.

Drain value 6 starts a previously idle retry count without incrementing it twice.
Subsequent events reduce the drain counter; reaching zero invokes the application
callback on the following event. Callback changes to the active/retry state are
visible before the remaining retry checks. Retry counters retain byte wrap.
The clock path reconstructs two timestamps, uses their signed low-16-bit
difference, detects a change greater than 2,000, and schedules a correction or
return-to-neutral through its callback. Both clock scaling settings, signed wrap,
strict threshold boundaries and pending-correction ordering are compared.

Fresh original execution covers **9,760 cases** and **253 instruction locations**,
bound to the matched payload/container/analysis ELF. Comparison checks ordered
effects, per-effect user/drift snapshots and final state, with varied first/second
users, all byte-sized request values, drain/retry boundaries, random timestamp
words and selected callback mutations. It includes 14,407 field writes, 1,549
field exchanges, 276 application callbacks, 2,692 retry-reset callbacks, 854
correction callbacks, 477 reset callbacks and 9,760 paired queue/observer events.
The original leaves two posted stack bytes uninitialized. Native clears them;
48 notification effects with nonzero original suffixes are recorded as deliberate
differences. Native also rejects invalid users/sources before effects.

Field operations, reset, MCS application, correction scheduling, queue posting
and observer callbacks are intercepted. Their transitive behavior is not proved
by this comparison. Log/time helpers are excluded. Timestamp snapshots do not
establish physical read ordering; most register races, callback failures and
actual IRQ/clock/RF timing remain open. Sanitizer tests exercise a multi-event
drain sequence, callback mutation, counter wrap, signed drift, reserved bytes
and invalid-input isolation.

Rechecking the matched original loader/dispatcher ranges and initial state
supports a disabled distance-correction gate on the static DEV loader path.
This is not a live read of the original globals and does not identify the cause
of low-power acquisition failure. The new service remains outside the runtime
until its producers, MCS/reset callbacks and ownership are compared and integrated.

Stage 191 leaves accepted stage 189 running. The standard build reproduces its
runtime/container hashes exactly; 18 Python tests pass. A fresh 30-second
`/dev/video0` HDMI-to-USB capture has 33 linked samples, advancing AP time and zero
sampled socket drops. Inspected frames show the dark textured camera scene and
OSD. This verifies the retained runtime, not hardware execution of the new helper
or controlled scene freshness/latency. Detailed comparison and capture records
remain in ignored `.local/`.

### TX MCS producer and application callbacks

Source-only `tx_mcs.c` now implements the MCS request producer, register
application, deferred-drain application, per-user initialization and retry-failure
callback. Requests preserve an outstanding target; an increase applies immediately,
while a decrease saves the previous index and starts the six-event drain.
Initialization clears the user's bookkeeping and derives its MCS from register
fields using the first matching entry in the supplied 25-key loader table.
Programming preserves the original mode transition, code-length calculation and
chip-specific 24-block coding adjustment. Historical probes also exercised the
dedicated user-10 mode register and user-11 mode-write exclusion. Stage 196
establishes that these hardware branches do not extend the software bookkeeping
table; the stateful native API now rejects users above 9.

Retry failure clears the selected user's active flag before event 9, which
contains the role-dependent user byte, requested/previous MCS, history and retry
count minus one. Retry count and history clear after event delivery; unrelated
callback mutations survive. Following the actual callback exposed a stage-191
interface omission: retry reset needs the affected user argument. The interface
now passes the second sampled user, and fresh original comparison checks it.
The omission was confined to the source-only helper; the runtime never used it.

Fresh execution compares **23,670 nonfatal scenarios** and separately checks
the effects/state prefix of **846 original assertion cases**, where native
returns `-95`. These are intentional failure-behavior differences, not original
error parity. Historical probes include all valid MCS indices, both roles, twelve
user indices across the callback/request cases, four representative indices across the
programming sweep, normal/special keys, bandwidth/interleave combinations, chip
selection, pending requests and event-callback mutation. Native rejects unsafe
users, indices and malformed key encodings. An error can follow a mode write or
request-cache update; this helper does not promise atomic failure.

The comparison includes **324 composed sequences**, each executing an original
request followed by eight original interleaver services with the actual MCS
application and retry-reset bodies. Ordered comparisons cover 72,719 field
writes, 82,890 field reads, 2,814 direct word reads, 516 event deliveries and
2,592 queue/observer pairs in the nonfatal cases. All 680 executed instruction
locations are checked against the matched payload/container/analysis ELF.
A separate fresh replay of the 9,760 stage-191 scenarios validates the corrected
callback argument and retains its 48 deliberate reserved-byte differences.

MMIO helpers, event delivery, queue/observer endpoints and diagnostics remain
intercepted. One multiply-subtract instruction absent from the installed partial
ISA model is modeled explicitly in the private runner from a pinned
[vendor specification](https://github.com/XUANTIE-RV/thead-extension-spec/blob/5a75db2696881a812a04e6e00347d3920dae03f2/xtheadmac/muls.adoc).
Its encoding is byte-checked; this is analysis metadata, not hardware proof of
the processor's complete ISA. The original assertion is a comparison boundary;
its fatal handler is not executed. Composed sequences use the disabled distance
gate and do not establish concurrent IRQ, buffer-ownership or physical RF timing.

Two sanitizer suites verify the combined request/drain/application sequence,
selected-user retry events, callback mutation, coding adjustment and validation
bounds. Eighteen Python tests pass. Stage 192 leaves the runtime unchanged; the
standard build reproduces accepted stage 189 exactly. A fresh 30-second direct
HDMI-to-USB capture shows the camera scene with 33 linked/AP-time samples and zero
sampled socket drops. This verifies the retained image, not hardware execution
of the new callbacks or controlled scene freshness/latency.

### TX pre-encode history and end notification

Source-only `tx_irq.c` implements the bookkeeping of the original pre-encode
handler at `0x221490` and TX-end handler at `0x21f778`. **History is recorded
during pre-encode, before completion.** The earlier description of it as
"TX completion history" was inaccurate. Neither the history bit nor a local
TX-end notification proves peer reception or acknowledgement.

Pre-encode marks the transmitter busy, samples its timestamp and remembers the
first hardware user. It posts queue 2/message `0x301` with that user, the budget
and request nibble, then samples the hardware user again for retry bookkeeping.
The second user can differ from the first. An active user with a nonzero retry
counter gets a history bit; only interleave mode exactly 1 uses the bank-dependent
pair of bits. Counter subtraction wraps as a byte and shifts use RV32 masking.
Field-read callback mutations are visible when the counter is reloaded.

TX-end clears busy only when its previous value is exactly 1, posts queue
2/message `0x309` for the remembered user and notifies source 11 with no payload.
Duplicate or stale end notifications still post; other busy values survive.
Queue failure does not suppress subsequent bookkeeping or observer notification.
These are endpoint contracts, not proof that a queued release actually executes.

Fresh original execution compares **4,050 pre-encode and 1,536 TX-end scenarios**:
twelve synthetic table indices, separately sampled users, active states, retry and
shift boundaries, interleave values, both banks, busy states, queue failures and
selected callback mutations. TX-end covers every remembered-user byte. Stage 196
corrects the supported bookkeeping range to 0..9; indices 10/11 were unsafe
original probes. Ordered
comparisons include 17,022 word reads, 2,466 field reads, 4,050 timestamp samples
and 5,586 queue/observer pairs, with user and IRQ state checked at each effect.
All 156 executed instruction locations match the reference payload, container
and analysis ELF. Diagnostics and their clock calls are intercepted.

Native clears the original's three uninitialized pre-encode suffix bytes;
324 queue/observer effects differ deliberately for this reason. Missing native
arguments are rejected before effects. An invalid second table user is rejected
after posting the first sample and still notifies, avoiding the original's unsafe
table access; this extra guard has sanitizer coverage, not original-error parity.
The comparison does not exercise real queue scheduling, packet construction,
descriptor retirement, interrupt races or physical RF timing. The module remains
outside the runtime while these integration dependencies are resolved.

Three sanitizer suites cover this helper and the preceding MCS/interleaver
helpers; 18 Python tests pass. The standard build remains byte-identical to
accepted stage 189. A fresh 30-second direct `/dev/video0` HDMI-to-USB capture
has 33 linked samples, advancing AP time and zero sampled socket drops. Inspected
frames show the dark textured camera scene and camera OSD. This verifies the
retained image, not hardware execution of the new helper or controlled scene
freshness/latency. Original traces, tool provenance and captures remain private.

### Retained-descriptor retirement

Source-only `tx_retire.c` implements the retirement loop at `0x230a80` using
bounded native packet views. The original first resolves a user context; an
absent context is a no-op. While its owned count exceeds the retention limit,
it releases each descriptor in the oldest packet, then advances the tail modulo
ten and decrements ownership. It preserves packet bytes, ring pointers and head.
These are local ownership events, not peer acknowledgements.

Each release receives context bits 24..27, the descriptor's low byte, a zero
direction and descriptor bits 12..31. The release return is ignored. The selected
packet pointer stays held across callbacks, while its descriptor count, later
descriptor words and context selector are reloaded. The native helper preserves
these mutation effects. The caller must serialize ownership and keep packet
storage alive throughout the call.

Fresh original execution compares **2,588 scenarios** and **9,857 ordered release
calls**, checking context and packet memory as well as arguments. Coverage includes
all ten tail positions and ownership depths zero through ten, retention limits
0/1/5/10/11/`UINT32_MAX`, all sixteen selectors, twelve synthetic lookup indices,
absent contexts, zero/multiple/255 descriptors, ring wrap and 384 callback-mutation
cases. All 49 executed instruction locations match the reference payload,
container and analysis ELF. The callback at `0x248998` is intercepted; this does
**not** establish its underlying stream queue consumption, callbacks or locking.

The native helper rejects invalid ring state and truncated packet storage. An
error can follow earlier releases and leaves the failing packet owned; blindly
retrying could release a descriptor twice. These bounds guards are deliberate
native behavior, not original error parity. The original accepts retention limits
larger than ten as a no-op on valid rings; this helper does too.

The current runtime uses a different, copy-owned packet representation with no
external stream buffers to release at retirement. Its existing
`ar_dev_tx_retire` bookkeeping matches **440 valid original zero-descriptor
scenarios** with all other native state preserved. Its rejection of limits above
ten differs deliberately in another 220 cases. This comparison establishes ring
bookkeeping, not equivalence of stream-buffer lifetime or backpressure.

Byte-checking the original pre-encode caller confirms that it reads the retention
limit from context bits 28..31. The current runtime uses a fixed limit of one;
TX-end uses zero. The earlier header's unconditional claim that stock retains
one has been corrected. Broader context configuration and ownership producers
remain integration dependencies.

Three sanitizer suites cover descriptor retirement, the existing copy-owned TX
path and interrupt bookkeeping; 18 Python tests pass. The standard build remains
byte-identical to accepted stage 189. A fresh 30-second `/dev/video0` HDMI-to-USB
capture has 33 linked samples, advancing AP time and zero sampled socket drops.
Inspected frames show the dark textured scene and camera OSD. This verifies the
retained runtime, not hardware execution of the new descriptor helper or
controlled scene freshness/latency. Detailed evidence remains private.

### Stream-release consumer and queue consumption

Source-only `tx_stream.c` implements the consumer behind descriptor retirement.
Fresh comparison executes the original consumer at `0x248998`, registry lookup
at `0x231db8`, reference drop at `0x231e28`, entry traversal at `0x21a614` and
`0x21a62e`, and queue consumption at `0x248228`. Composed cases also execute the
original retirement loop. These bodies replace stage 194's intercepted release
endpoint in the new comparison.

Lookup accepts selectors 0..7 and streams 0..4; any nonzero direction selects
the alternate direction. It increments a found object's reference counter.
The consumer subsequently drops two references, each saturating at zero.
This count behavior is compared, but it does not by itself establish which
producer owns the additional reference or when its storage may be destroyed.
The [subsequent acquisition comparison](#stream-acquisition-and-reference-ownership)
identifies the held reference; storage creation/destruction and concurrent
ownership still need separate comparison.

With flag bit 2 clear, the consumer removes the requested byte count from the
queue if enough bytes are present. With that bit set, it advances retained-entry
consumed offsets. An entry is complete only when consumed equals produced and
produced equals the full 32-bit length. Its optional buffer callback runs before
the entry is removed. Completed entries are then consumed as 20-byte queue
records; queue cursor/count updates precede the wake call. A partial or not fully
published entry stops traversal.

The retained path subtracts the **requested** byte count from its accounting
counter even when fewer bytes were consumed. This subtraction and the available
byte calculation preserve unsigned wrap; consumed offsets remain 16-bit.
The helper does not reinterpret successful return as proof that all requested
bytes existed. Missing queues, zero requests and absent registry entries have
separate compared paths. Lock return values are ignored by these original bodies;
the adapters preserve that behavior without claiming it is safe under an actual
RTOS failure.

Fresh comparison covers **3,600 scenarios**, including **96 composed retirement
sequences**, and checks state at every recorded effect. Cases include both
consumer paths, partial/full/unpublished/empty and wrapped entry arithmetic,
queue cursor wrap and normalization, out-of-range selectors/streams, absent
objects/queues, reference wrap and selected callback mutations. Comparisons cover
13,628 lock calls, 13,628 unlock calls, 1,288 wakes and 418 buffer callbacks.
Nineteen cases execute mutations to references, accounting and mutex tokens;
later operations observe them. All 294 executed instruction locations match the
reference payload, container and analysis ELF.

RTOS lock/unlock, wake, buffer callback bodies and diagnostics remain intercepted.
This does not prove scheduler behavior, mutex correctness, producer lifetimes,
DMA concurrency or the host's backpressure contract. Native bounds guards reject
unsafe entry metadata and missing adapters; these are additional errors, not
original error parity. A guarded error releases acquired locks and drops the
references but cannot undo prior buffer callbacks or consumed-offset changes.
The selected stream, queue and entry storage must stay alive throughout the call.

Three sanitizer suites cover the composed consumer/retirement path, partial
consumption, mutation, accounting wrap and guarded cleanup, plus the existing
socket implementation. Eighteen Python tests pass. The new helper remains
outside the runtime; the standard build reproduces accepted stage 189 exactly.
A fresh 30-second direct `/dev/video0` HDMI-to-USB capture has 33 linked samples,
advancing AP time and zero sampled socket drops. Inspected frames show the dark
textured scene and camera OSD. This verifies the retained firmware, not hardware
execution of this consumer or controlled scene freshness/latency.

### Corrected TX bookkeeping bounds

Stage 196 corrects an error in the source-only TX helpers and earlier coverage
descriptions: the original bookkeeping table has **ten 12-byte records**, not
twelve. The original initializer loop at `0x22520e..0x225238` programs and
initializes users 0..9. The next region, starting at `0x26a9ac`, is used by the
callback-list registration/unregistration dispatch paths. Hardware helpers with
special handling for user 10 do not establish another software bookkeeping slot.

Fresh execution runs 75 scenarios with three memory patterns. Sixty direct
program/initialization cases and three complete initializer-loop excerpts match
native behavior and preserve the following 56 bytes. Twelve deliberately unsafe
original probes of users 10/11 modify that adjacent region: initialization
overwrites twelve bytes and programming updates a byte there. Native now rejects
these calls before effects. All 219 executed instruction locations and 53 static
instructions in the adjacent callback-list dispatch ranges are byte-checked
against the matched payload/container/analysis ELF. The full loader and callback
registration paths are not executed in this new experiment.

`ar_interleaver` now holds ten records. MCS control and interleaver service reject
out-of-range bookkeeping users before callbacks. Pre-encode still posts its
first hardware sample, then rejects an unsafe second table index and notifies
without accessing that record. TX-end still carries the full remembered-user
byte, since it does not index the bookkeeping table. These guards are deliberate
native behavior, not replication of original out-of-bounds accesses.

Replay of the prior byte-bound traces, using the corrected native structure,
passes **7,540 interleaver cases**, **17,496 nonfatal MCS cases plus 534 original
assertion-prefix cases**, and **4,884 interrupt cases**. The MCS replay includes
270 composed request/interleaver sequences. State comparisons also require the
adjacent original bytes to remain unchanged. The 9,408 earlier probes with unsafe
bookkeeping indices are excluded from supported-user coverage; their historical
trace receipts remain preserved. Replay is not a new original emulator run.
The reserved-byte differences and assertion behavior remain explicit exceptions.

The corrected bounds have sanitizer regressions covering users 10..15 and all
MCS actions, checking rejection ordering and state preservation. Three sanitizer
suites and 18 Python tests pass. These helpers have never been linked into the
running firmware, so the device was unaffected. The standard build reproduces
accepted stage 189. A fresh 30-second direct HDMI-to-USB capture has 33 linked
samples, advancing AP time and zero sampled socket drops; inspected frames show
the dark textured camera scene and OSD. No controlled freshness/latency claim is
made. Retry-limit and ownership producers remain under comparison.

### Stream acquisition and reference ownership

Stage 197 extends source-only `tx_stream.c` with acquisition, comparing the
original body at `0x232ca6` and its actual lookup, reference-drop, entry traversal,
memory clear/copy and software arithmetic routines. A nonempty result retains
the reference added by lookup. Release looks the object up again, then drops
both references. Empty acquisition drops its lookup reference. This establishes
that local acquire/release relationship; it does not establish object lifetime
through allocation, teardown or concurrent DMA.

The requested budget multiplies the request by the stream's percentage byte,
wraps the product to 32 bits, divides by 100 and truncates, then clamps zero to
one. The native implementation uses integer division; comparison executes the
original floating-point conversion/division instructions and checks the original
100.0 constant against the matched payload. The two storage paths differ:

- Retained entries return at most five address/length spans, skip fully produced
  entries, and advance 16-bit produced offsets with wrap. Acquisition leaves the
  pending-byte accounting counter unchanged. It traverses from the queue's read
  position toward its write position under the queue mutex.
- The byte ring returns at most two spans, advances a separate acquisition
  cursor, subtracts from available bytes and adds to a 64-bit acquired counter.
  Its available count gates access; span sizes come from the cursor geometry,
  preserving the original subtraction wrap for inconsistent count fixtures.
  Disabled queues return empty. Empty enabled queues can retry after waits:
  finite deadlines use signed 32-bit millisecond differences; `UINT32_MAX`
  requests repeated 100-ms waits. Lock and wait return values are ignored.

Zero requests return `-1` without clearing output. A missing stream returns `-1`
after clearing only the first two spans. Successful retained mode writes five
spans; byte mode writes two and leaves the remaining three untouched. Missing
queues return empty success with the corresponding output size. These details
are included in state/effect comparisons rather than normalized away.

Fresh original execution matches **3,356 scenarios**: 1,629 retained-entry and
1,727 byte-ring cases, including **128 composed acquire/release sequences**.
The cases cover budget and address wrap, produced-offset wrap, empty/full/partial
entries, five-span limits, ring boundaries, missing objects/queues, selectors,
directions, reference wrap, disabled queues and finite/infinite wait behavior.
Eighteen cases wait; selected wait fixtures publish bytes, disable the queue or
change mutex tokens. Comparisons check state at 7,717 lock and 7,717 unlock calls,
1,668 clock calls, 24 waits, 100 wakes and 36 buffer callbacks. All **728 executed
instruction locations** match the original payload, container and analysis ELF.
Twelve additional release-diagnostic clock calls are recorded but excluded from
native effects, consistent with the previously intercepted diagnostic path.

Replay of the earlier release traces also passes all **3,600 scenarios**,
including 96 composed retirement sequences. This is regression replay with the
expanded native structures, not new original execution. RTOS endpoints, clocks,
wait publications and buffer callbacks remain synthetic. The partial ISA model,
analysis ABI and pointer tokens are analysis metadata. Native guards reject
unsafe storage and missing adapters, release acquired locks and drop the lookup
reference; they cannot undo earlier produced-offset changes. Their `-22` errors
are additional native behavior, not original error parity.

Three sanitizer suites and 18 Python tests pass. The standard build remains
byte-identical to accepted stage 189, which stays running on the device. A fresh
30-second direct `/dev/video0` HDMI-to-USB capture has 33 linked samples,
advancing AP time and zero sampled socket drops. Inspected frames show the dark
textured camera scene, right-side features and camera OSD. This validates the
retained runtime, not hardware execution of the new helper or controlled
freshness/latency. Packet-builder composition, enqueue/allocation/teardown,
backpressure, concurrent ownership and runtime integration remain open.

### Prepared TX packet assembly and retirement

Stage 198 adds source-only `tx_packet.c` and the availability scan in `tx_stream.c`.
Comparison executes the prepared-packet phase at `0x238848..0x238b38`, starting
with synthetic entry registers after ring-slot and outband selection. It includes
the actual original lookup/drop, traversal, acquisition, arithmetic, memory and
header-checksum routines. Eighteen successful cases then execute original
descriptor retirement, stream release and queue consumption. This joins the
previously separate ownership comparisons; it does not execute the full
pre-encode handler, prior reservation, or subsequent accounting.

Availability scans all five ports before selecting descriptors. Retained queues
sum `length - produced` modulo 2^32 across their entries; byte queues expose their
available count. Negative signed totals become zero. Lookup/drop and queue locks
surround this scan, but the scan does not reserve those bytes. Selection proceeds
in port order with four bytes of descriptor overhead for each selected port.
The builder preserves descriptor bits 8..11, records the requested length, calls
acquisition with timeout zero, and ignores its return code. Stream spans are
stored by port, while descriptors occupy a compact list.

Compared gates include role/mode bytes, registry presence and user flag bytes.
An optional policy path reads a value from `0x2203ca` and a word from `0x229124`.
When the word's high half multiplied by 120 is nonzero and below the value, it
updates a control nibble and limits selection to port 0. Those two endpoints are
intercepted: their producers, units and full semantics are not established by
this test. They must not yet be described as a verified backpressure mechanism.

The builder advances the sequence byte, sets the outband flag, computes the
header checksum, then invokes cache maintenance and segment submission for the
header, optional outband, and each nonempty span in descriptor order. It checks
the submitted total against its budget accounting afterward. Successful cases
record the request byte and issue the terminal submission. **A length mismatch
reaches an original assertion path after segment submission**, without a terminal
submission or reference rollback. Such mismatches occur in fixtures with scaled
acquisition, disabled byte queues, or more retained entries than fit in five spans.
The native helper returns `-95` at that boundary and preserves the partial effects.
Blindly retrying or retiring that packet is unsafe: its advertised lengths can
exceed what acquisition supplied. A runtime recovery policy remains necessary.

Fresh comparison covers **492 scenarios**, including **448 nonfatal outcomes**
and **44 assertion prefixes**. It checks 704 descriptors, 7,540 lock/unlock pairs,
1,802 cache calls, 2,250 segment/terminal submissions, 437 clock calls, 24 policy
value calls, 12 policy word calls, 23 wakes and 18 buffer callbacks. The **18
composed retirement sequences** include empty and populated packets. State is
compared at every recorded effect and at completion/failure. All **967 executed
instruction locations** match the original payload, container and analysis ELF;
the original checksum table remains private and its hash is recorded.

RTOS/clock, policy, cache and submission endpoints are intercepted. Original
assertion handling is not executed after the mismatch boundary. The comparison
uses synthetic queue/registry/ring storage and an analysis ABI, with no concurrent
mutation. Native `-22` guards and `-95` returns are deliberate errors rather than
copies of original unsafe accesses or fatal handling. Ring reservation, policy
producers, allocation/teardown, post-submission accounting, interrupt ordering and actual DMA
ownership remain open, as does integration with the runtime's current copy queues.

Four sanitizer suites cover the assembled packet through native retirement,
segment ordering, gates, span shortfall, checksum and guarded cleanup. Eighteen
Python tests pass. All **3,356 prior acquisition scenarios** pass replay against
the updated stream module; that replay is not fresh original execution. The
standard build is byte-identical to accepted stage 189. A fresh 30-second direct
`/dev/video0` HDMI-to-USB capture has 33 linked samples, advancing AP time and zero
sampled socket drops. Inspected frames show the textured camera scene, right-side
features and camera OSD. The accepted firmware remains running; no hardware
execution of this new helper or controlled freshness/latency is claimed.

### TX slot reservation and post-submission accounting

Stage 199 extends fresh original execution from `0x2387b6` through prepared
packet assembly, counter resets and the statistics body at `0x22fd9c`. Selected
cases then retire the packet through the original release consumer. Entry
registers still represent a prepared context after earlier outband selection;
this is not full pre-encode-handler execution.

Reservation selects the current head slot, advances head modulo ten and increments
the owned count. A full ring reaches the original assertion path without those
changes. A null selected packet reaches it **after** both counters change. Native
`ar_tx_reserve` returns `-95` at those boundaries. It neither validates the entire
packet storage nor unwinds reservation when later preparation fails. Additional
native argument/head guards return `-22` before accessing unsafe storage.

The code after successful submission performs accounting, not a host notification.
For a present selector 0..7 with a nonzero reset mask, it holds the registry mutex
and examines ports 1..4 in order. Each selected queue's 64-bit acquisition counter
is cleared under its queue mutex. A missing selected stream or queue stops the
loop, preserving resets already performed. The entire mask is then cleared,
including bits outside ports 1..4. No lookup references are added or dropped.
Selectors outside that range and absent user tables skip resets but still record
statistics. Lock return values remain ignored in the compared original path.

The statistics ring records 16-bit budget and submitted lengths plus a 32-bit
millisecond timestamp. Its head advances modulo its configured capacity. Three
64-bit totals accumulate the truncated budget, submitted length, and
`submitted - header` modulo 16 bits, respectively. That final value includes
outband bytes; it is not necessarily host payload alone. Counter overflow is
preserved. Original storage allocation and valid capacity producers remain open;
the native view supplies explicit storage bounds and rejects invalid metadata.
Such rejection occurs after any stream resets already performed.

Fresh comparison covers **537 scenarios**: **487 nonfatal outcomes** and **50
assertion prefixes** comprising 44 length mismatches, three full rings and three
null slots. There are **27 reserve/prepare/account/retire sequences**, including
empty and populated packets. Cases include last-slot reservation, near-full
rings, missing streams/queues, reset-mask combinations, field truncation, timestamp
wrap and 64-bit counter wrap. State comparisons cover 799 descriptors, 9,162
lock/unlock pairs, 1,987 cache calls, 2,474 submissions, 974 clock calls, 24 policy
value calls, 12 policy word calls, 68 wakes and 60 buffer callbacks. All **1,123
executed instruction locations** match the reference payload/container/analysis
ELF. Eighteen missing-stream/queue diagnostic clock calls are recorded separately
and excluded from native effects; diagnostic formatting remains intercepted.

RTOS/clock, policy inputs, cache maintenance and segment submission remain
intercepted. The tests use synthetic registry, queue, ring and statistics storage;
the ABI and pointer mappings remain analysis metadata. They do not establish
concurrency, cache effectiveness, actual DMA or original allocation/lifetime rules.
Native error returns replace unsafe access or fatal continuation, preserving the
documented prior effects. Recovery and integration with the current copy queues
remain separate requirements.

Four sanitizer suites and 18 Python tests pass. The standard build reproduces
accepted stage 189 exactly. A fresh 30-second direct `/dev/video0` HDMI-to-USB
capture has 33 linked samples, advancing AP time and zero sampled socket drops.
Inspected frames show the textured camera scene, right-side features and camera
OSD. The accepted firmware remains running. This validates the retained image,
not hardware execution of these source-only helpers or controlled scene freshness.

Next, compare the remaining ownership and retry-limit producers, connect the
compared TX helpers to the runtime, and isolate low-power acquisition
before retrying host power integration. Extend the core
inventory through transport dispatchers
and indirect callbacks, binding each contract to its comparison tests. Then cover
radio messages, timers and state transitions in the same way. Dynamic power still
needs a separately isolated acquisition fix. Neither a large synthetic test count
nor a visible scene can close the remaining matrix by itself.

### Runtime TX MCS drain integration

Stage 200 connects the compared TX MCS helper and the TX bookkeeping portion of
interleaver service to the normal mode-0 DEV runtime. Initialization derives the
current user-0 MCS from programmed fields. Incoming command 3 still requires
slot 0, an index below 16 and a supported normal encoding key. Increases apply
immediately; equal targets do nothing. A decrease preserves its pending target
against subsequent requests, counts six user-0 interleaver events and applies on
the seventh. A second hardware-user read selects the bookkeeping entry; other
users do not advance user 0. Successful application publishes the existing
MCS-change event. The compared chip-profile-1 block-count adjustment is included.

This is a partial integration into the existing cooperative copy-mode TX path.
It does not install the original full interrupt handler, queue delivery or
retained-buffer ownership. Retry limits remain zero: pre-encode history and
retry-failure event producers are not integrated. Other bookkeeping users,
special PHY keys and cancellation with outstanding hardware work remain gaps.
The 64-byte TX diagnostic uses its former final three reserved words for pending
state (`active | drain << 8 | target_mcs << 16`), completed drain applications
and requests ignored while busy.

The refactored helper replays 7,540 original interleaver scenarios and 18,030
TX MCS scenarios: 17,496 nonfatal outcomes and 534 assertion-prefix comparisons.
The latter set includes 270 composed request/interleaver sequences. These reuse
the corrected, hash-bound traces described above, with 2,220 and 6,486 unsafe
user probes respectively excluded. Forty interleaver effects differ only in
reserved payload bytes that native code deliberately initializes. Intercepted
MMIO/RTOS helpers, synthetic state, partial ISA semantics and synthetic ELF/ABI
metadata retain their earlier limitations; replay is not fresh hardware or full
handler execution.

Three sanitizer suites and 18 Python tests pass. The synthetic RF integration
test exercises immediate increase, pending decrease, competing requests,
other-user isolation, six drain steps, seventh-event application, register
fields, unchanged command duration and exact event ordering.

Hardware validation uses the matched Ascent VRX 18.21.10 inputs after a receiver
reboot into stock. Direct `/dev/video0` capture through the HDMI-to-USB adapter
uses MJPEG 1920×1080 at nominal 30 fps, with device identity and settings recorded
privately. Native startup, stop/start, deinit/init/start and candidate
removal/readmission captures total 80 seconds and 92 linked/AP-time samples,
with zero sampled socket drops. Inspected frames show the same dark textured
camera scene and right-side features as fresh stock. Compensation survives the
transitions, and the normal backend causes one TX MCS increase from 1 to 2 in
each session. **No decrease is observed on hardware.** Consequently this trial
establishes baseline video/lifecycle compatibility and increase application;
the new decrease path retains offline and synthetic integration evidence only.
Physical peer loss, sustained adaptation, controlled freshness/latency and the
previous low-power acquisition failure remain unresolved.

The retained runtime is 72,528 bytes, SHA-256
`83a2ca3937356399ce8e6369b138985e77c3cad4d51862bdb578ce62bf320bed`.
Its 90,624-byte container has SHA-256
`ea5bc92ea9b44cd41e10221e3e94bb9c1fd77ce3edf7b8948b0df729bd7f469c`.
The standard build reproduces it; RAM usage is 676,976 of 704,512 bytes. The
bootstrap and private board profile are unchanged. Full original-firmware
coverage remains incomplete.

### Runtime TX attempt history

Stage 201 connects the compared pre-encode history step to normal user-0 TX.
After capturing the packet request fields, the runtime samples the hardware user
again and updates history for user 0 before packet construction. Active
bookkeeping and a nonzero retry counter are required. The existing helper
preserves byte subtraction, RV32 shift masking and paired bits for interleave
mode exactly 1. A zero budget can prevent submission after history is recorded:
this remains an **attempt history, not a completion or acknowledgement mask**.
Other hardware users are counted and skipped by this runtime integration.

Initialization continues to clear the retry limit. This stage does not supply a
new retry-limit policy or establish that every original caller leaves it zero.
The full pre-encode/TX-end queue handlers, timestamps, observers, retained-buffer
ownership and other users remain separate integration work. A cooperative call
before packet construction does not prove original IRQ/task scheduling parity.

The new project-only read diagnostic `0x0c5a0028` (daemon alias `0x015a0028`)
returns eight little-endian 32-bit words: sampled pre-encode events, last sampled
user, other-user samples, eligible user-0 attempts, history mask, packed
bookkeeping, target MCS and previous MCS. Packed bookkeeping is
`active | retries << 8 | limit << 16 | drain << 24`. Eligibility counts calls
that can set a bit, even if that bit was already set. This endpoint does not
inject requests or change radio state. Context preparation resets these counters
and history; stopping alone preserves the diagnostic snapshot.

Five ASan/UBSan suites and 18 Python tests pass. The synthetic RF test records
history during a pending decrease with zero packet budget, exercises both banks
and other-user isolation, and confirms that application ends further history
updates. It also checks reset on new preparation. The extracted helper preserves
4,884 corrected original IRQ replay cases, including 216 deliberate differences
in uninitialized payload suffix bytes. Unsafe table probes remain excluded as
described in the earlier bounds audit.

Fresh original execution compares **270 composed scenarios** across all ten
bookkeeping users: a request followed by eight rounds of interleaver, pre-encode
and TX-end handlers. The original application and retry-reset bodies execute.
The cases cover increase/equal/decrease requests, seeded retry boundaries and
limits, and alternating bank samples. All 592 executed instruction locations
match the reference payload/container/analysis ELF. Ordered comparisons include
3,080 field reads, 2,610 field writes, 8,940 direct word reads, 6,480 queue posts,
6,480 observer calls and 30 retry events. Synthetic retry seeds do not establish
runtime policy producers. Clock, diagnostics, field MMIO, queue/observer and event
delivery endpoints are intercepted; no real packet ownership or RF delivery
executes in this comparison. Synthetic ELF, ABI and partial ISA semantics remain
analysis metadata.

Hardware testing uses freshly rebooted stock and the same matched Ascent VRX
18.21.10 inputs. Unlike stage 200, the peer requests decreases in this trial:
startup records one completed drain and five eligible attempts; restart records
one drain and three attempts; candidate readmission records three drains and
thirteen attempts. All three sessions finish at TX MCS 2 with history mask 1.
Restart/readmission samples directly observe MCS 2 → 1 → 2 and the corresponding
TX coding/rate field changes. No requests are rejected in these snapshots.
This provides hardware evidence of the normal-key decrease/history path, while
exact six-event timing remains covered by original execution and synthetic tests.
Neither local register changes nor history establish peer acknowledgement.

Startup, a separate post-drain check, stop/start, deinit/init/start and candidate
removal/readmission have **110 seconds of direct HDMI-to-USB capture**, with 125
linked samples, advancing AP time and zero sampled socket drops. Device identity
and MJPEG 1920×1080/nominal-30-fps settings are recorded privately. Inspected
frames retain the same dark textured camera scene as stock. Calibration survives
the transitions; restart and reinitialization clear history before new traffic.
Controlled freshness/latency, physical peer silence, sustained loss/adaptation,
low-power acquisition and full firmware parity remain unresolved.

The retained runtime is 72,912 bytes, SHA-256
`baad0d33a1a60b5417fbdf8e981af8fa4c2216af2d5dd950b78124c175110428`.
Its 91,136-byte container has SHA-256
`5a574f6b296eb8aa4eee3aaf09c258b97735727848822776dfdb560183db36c5`.
The standard build reproduces it; RAM usage is 677,520 of 704,512 bytes.
Bootstrap and private board-profile hashes are unchanged.

### TX queue producer and backpressure

Stage 202 adds a source-only queue producer to `src/tx_stream.c`, compared with
the matched original body at `0x21b500`. This supplies the write side of the
previous acquisition/release work. It does not replace the runtime socket copy
path or establish retained-entry publication and socket teardown behavior.

The original distinguishes occupied bytes from bytes still available for TX
acquisition. Its discard option rewinds the write cursor by the available count,
subtracts those bytes from occupancy and the 64-bit written counter, and clears
availability. Already acquired bytes remain occupied. Discard happens before
the whole-request space check, so a `-12` rejection can still have discarded
queued data. Discard is applied again on each wait-loop pass; the returned copy
count is therefore not a guarantee that every counted byte remains queued.

Writes use up to two free spans around the ring. Every pass publishes copied
bytes, updates the high-water and written counters, and wakes readers while the
queue mutex is held, even when no bytes fit. The caller-supplied pending increment
is added only when the full request has been copied. Partial nonblocking writes
return their copied count and increment the short-write counter. Disabling the
queue during a wait returns `-10`, even if earlier passes already published bytes.
Null queue/input or zero length returns `-6` without a queue operation.

Positive timeouts use a wrapping millisecond deadline and waits capped at 100 ms.
Every signed-negative timeout selects indefinite 100-ms retries. This differs
from the acquisition helper's handling of negative values other than `-1`.
Lock and wait results are ignored in the compared original path. The native API
adds `-22` guards for unsafe storage metadata and missing adapters; these guards
are not original error behavior. Callers must provide stable, nonoverlapping
storage, serialization and eventual wait/clock progress.

Fresh original execution passes **1,554 scenarios**, including **18 composed
write/acquire/release sequences** and 64 scenarios that wait. The comparison
checks copied bytes, return values, every queue field, reference changes, output
spans and ordered effects: 1,742 clock reads, 1,854 locks and unlocks, 1,355 wakes
and 200 waits. Cases cover both free-span layouts, full queues, partial writes,
discard with acquired bytes, counter wrap, deadline wrap, disable and mutex/signal
changes across waits. All 745 executed instruction locations match the reference
payload, container and analysis ELF bytes.

The original copy, arithmetic, acquisition, registry lookup/drop, release and
queue-consumption bodies execute. RTOS locks, waits/wakes and clock endpoints are
intercepted. Wait fixtures simulate consumption or disable; they do not prove
scheduler or DMA concurrency. Synthetic ELF, ABI, pointers and partial ISA
semantics remain analysis metadata. Native malformed-storage guards and large
address-space limits are outside this bounded original comparison.

Four ASan/UBSan suites and 18 Python tests pass. The standard build reproduces
stage 201 exactly. A fresh 30-second `/dev/video0` HDMI-to-USB capture records
MJPEG 1920×1080 at nominal 30 fps; inspected frames at 5 and 27 seconds show the
camera texture and right-side features. All 33 samples stay linked with advancing
AP time and zero sampled socket drops. Device image/configuration hashes and
retained-session identity are recorded privately. Stage 201 remains running in
RAM; this capture confirms continuity, not hardware integration of the new helper
or controlled video freshness. Runtime ownership, pending-DMA teardown and full
original-firmware coverage remain open.

### Stream close and held-reference lifetime

Stage 203 adds source-only stream close and queue disable, compared with the
original close (`0x231ef6`), stream destruction (`0x231e5e`) and queue disable
(`0x242f62`) bodies. This establishes another prerequisite for runtime ownership
integration. It does not replace the current host socket close implementation.

Close accepts selector 0–7, port 0–4 and a direction mask: bit 1 selects TX,
bit 0 RX, processed in that order. Invalid indices or no selected direction
return `-6`; a missing selector allocation returns `-2`. Higher mask bits do not
select additional directions. The native registry records selector allocation
separately because an allocated selector with empty stream slots closes
successfully.

A stream with nonzero references remains registered. Close disables its queue
under the queue mutex, unlocks the registry, delays one scheduler unit, and
retries indefinitely. Disable clears queue flag bit 1 and increments a wrapping
counter only if the queue was enabled. It does not wake readers or writers.
The delay endpoint's argument is compared; actual scheduler timing is not.

At zero references, close destroys the queue's ready signal, space signal and
mutex in that order. It frees queue storage unless queue flag bit 0 marks
external storage, clears the stream's queue pointer, frees the stream, and clears
the registry slot. Destruction runs under the registry lock. Queue occupancy does
not independently prevent destruction, and this destructor does not release
queued retained-buffer callbacks. Higher-level shutdown must therefore satisfy
the ownership contract; disabling a queue alone does not establish DMA completion.

After destroying TX, close clears its local TX selection bit before considering
RX. A new TX stream published while close waits for RX is left alone. If TX was
initially absent, the bit remains selected and a newly published TX stream is
visited on retry. Both cases are compared, along with missing queues, external
storage, unequal TX/RX reference counts, repeated disable, counter wrap,
re-enabling during a wait, and changes to synchronization tokens and allocation
flags during destruction callbacks.

Fresh original execution passes **1,064 scenarios**, including **24 composed
acquire/close/retire sequences** with byte and retained-entry queues. In those
sequences, acquisition holds a reference, close disables the queue, and the
intercepted scheduler delay runs the original packet retirement and stream release
bodies before close resumes and frees storage. The comparison checks complete
queue/stream views, registry slots, packet-ring state, acquisition spans, results
and ordered effects. It covers 506 waiting scenarios, 2,794 locks and unlocks,
1,716 synchronization-destruction calls, 1,196 frees and 1,058 delays. All 848
executed instruction locations match the reference payload/container/analysis ELF.

RTOS locking, delay, synchronization destruction, heap free, buffer callbacks and
diagnostics are intercepted. The 86 invalid-argument diagnostic clock reads are
excluded from native functional comparison. Synthetic retirement during a delay
does not prove concurrent scheduling or real DMA completion. Selector storage is
held stable across retries. Synthetic ELF, ABI and partial ISA semantics remain
analysis metadata; missing native adapter guards return an additional `-22`.

Five ASan/UBSan suites and 18 Python tests pass, including actual native frees
under ASan and stream publication during close. The previous 1,554 producer
scenarios still pass. The standard build reproduces stage 201 exactly. A fresh
30-second `/dev/video0` HDMI-to-USB capture at MJPEG 1920×1080/nominal 30 fps
shows the camera texture and right-side features at 5 and 27 seconds. All 33
samples remain linked with advancing AP time and zero sampled socket drops.
Device image/configuration and retained-session identity are recorded privately;
stage 201 remains running. Stream allocation, retained-entry publication, host
socket shutdown, runtime ownership and hardware pending-DMA tests remain open,
as do controlled video freshness and full original-firmware coverage.

### Handle lookup and close sequencing

Stage 204 adds `src/tx_handle.c`, a source-only handle-table adapter compared
with original lookup (`0x21a014`), snapshot/update (`0x219f9a`) and handle close
(`0x2492f8`). These are the handle operations used above the stream-close path;
they do not implement the host socket worker lifecycle.

A table record occupies 12 bytes: little-endian flags at byte 0, selector at
byte 4 and port at byte 8. Lookup preserves every byte. Flag bit 0 marks an active
handle; bits 1–2 select RX/TX for stream close. An out-of-range handle returns
`-1` without copying or locking. An in-range lookup copies under the table mutex
and returns success from the copied active bit, even if that bit changes in the
table before return. The update variant can clear the table's active bit while
still returning the previous record. Native storage bounds and missing adapters
have additional `-22` guards, outside the original error contract.

Handle close first snapshots the record while preserving its active bit, then
closes the selected streams. The table mutex is released before stream close,
so it is not held while references drain. A stream-close error is propagated
without a final table update. After success, handle close looks up the record
again and clears its active bit. This second read uses the current table record,
not the saved record. If the handle became inactive or the table count shrank,
it can return `-1` after streams have already been destroyed; those effects are
not rolled back. High flag bits and all unrelated record bytes are preserved.

Fresh original execution passes **635 scenarios**, including **24 composed
acquisition/handle-close/packet-retirement sequences** with byte and retained
queues. The comparison covers ordinary and updating lookup, valid/inactive/
out-of-range handles, direction masks, selector/port errors, missing selector
allocation, external queue storage, and changes to active bits, record contents,
table count and mutex tokens at callback boundaries. It compares complete
records, queue/stream and registry state, packet retirement, results and ordered
effects. All 951 executed instruction locations match the reference payload,
container and analysis ELF. The traces contain 993 locks and unlocks, 498
synchronization-destruction calls, 248 frees and 48 scheduler delays.

Locking, delay, synchronization destruction, heap free, buffer callbacks and
diagnostics are intercepted. The 113 invalid-close diagnostic clock reads are
excluded from native functional comparison. The composed cases run the original
stream close, acquisition, retirement and release bodies, but retirement during
a simulated delay does not establish actual scheduling or DMA completion.
Synthetic ELF, ABI, native pointer views and partial ISA remain analysis metadata.

Six ASan/UBSan suites and 18 Python tests pass. The standard build reproduces
stage 201 exactly. A fresh 30-second `/dev/video0` HDMI-to-USB capture at MJPEG
1920×1080/nominal 30 fps shows the camera texture and right-side features at
5 and 27 seconds. All 33 samples remain linked with advancing AP time and zero
sampled socket drops. Image/configuration hashes and retained-session identity
are recorded privately; stage 201 remains running in RAM.

The original host close branch subsequently posts an empty message to the
socket worker and clears its running flags. Worker draining, notification,
resource cleanup and host response behavior require their own comparison before
replacing the runtime socket close. Handle allocation/publication, retained TX
publication, pending-DMA hardware tests and full firmware coverage remain open.

### Socket worker shutdown and close replies

Stage 205 adds `src/socket_shutdown.c`, a source-only model of the host close
request, TX/RX worker shutdown and worker-exit notifications. These steps describe
the original shutdown ordering; the live polling endpoint still has its previous
copy-mode close implementation.

For a found socket, the original dispatcher calls handle close, makes an empty
message, posts it to the TX worker queue without waiting, then clears both worker
running bytes. It ignores the handle-close and post results. A missing socket
instead releases the request message and posts an empty reply with status `0x103`.
Closing a found socket does not produce the final host reply at this point.

When TX observes its running byte clear, it posts a `0x0410…` exit notification
before draining its input queue. Every successfully received queued message is
released. A nonempty message encountered while retained-mode flag `0x20` is set
decrements the pending-retained counter and increments drain/completion counters,
all modulo 2³². Any receive error ends draining. The worker then destroys its
queue, clears the queue token and deletes its current task. RX releases a held
copy buffer, if any, before posting a `0x0411…` exit notification and deleting
its current task. The worker notification posts use an indefinite timeout, but
their failure results do not undo cleanup.

The dispatcher clears the matching direction bit when it receives each exit
notification, releases the message, and rechecks the live direction flags. Once
both directions are clear, it clears the entire socket flags byte and posts a
`0x0403…` close reply. Both notification orders are compared. This ordering is
significant: receiving the TX notification alone does not prove its message drain
or task deletion has finished. Callback changes to flags can also affect retained
counter updates. Safe runtime adaptation still requires the scheduling and
ownership contracts, not merely reproducing these notifications.

The empty-message constructor changes the message representation and zeroes its
length; it preserves other fields. Worker scratch can contain earlier message
metadata or stack residue, so its message ID/status and unused bytes are not
assumed to be stable close-reply requirements. The native helper requires callers
to initialize notification scratch. Its 88-byte adapter representation and
format tokens are analysis views, not declarations of the SDIO wire format.

Fresh original execution passes **575 scenarios / 639 phases**, including
**16 composed request/TX/RX/exit-notification sequences**. The dispatcher entry
at `0x240782`, socket lookup at `0x23925a`, empty constructor at `0x23a052`, and
TX worker entry at `0x24029e` execute. TX starts with its running byte clear; RX
starts at the shutdown decision `0x23bbd4` with a synthetic held buffer/frame.
All 567 executed instruction locations match the reference payload, container
and analysis ELF. Ordered comparisons include 449 posts, 352 receives, 499
message releases, 126 queue destructions, 252 task deletions and 118 RX-buffer
releases. There are 226 drained messages and 214 deliberately failed posts.

Handle close, queue post/receive, virtual message methods, queue destruction,
RX-buffer release, task deletion and diagnostics are intercepted. The handle
ownership bodies remain covered separately by stages 203–204; they do not execute
inside this comparison. Composed sequences use successful posts and deterministic
phase order. This does not establish actual scheduler behavior, pending-DMA
completion, message-pool reclamation or the original active worker paths.
Synthetic ELF, ABI, pointers and partial ISA semantics remain analysis metadata.

Four ASan/UBSan suites and 18 Python tests pass. The standard build reproduces
stage 201 exactly. A fresh 30-second `/dev/video0` HDMI-to-USB capture at MJPEG
1920×1080/nominal 30 fps shows the camera texture and right-side features at
5 and 27 seconds. All 33 samples remain linked with advancing AP time and zero
sampled socket drops. Device image/configuration hashes and retained-session
identity are recorded privately; stage 201 remains running in RAM. Runtime
shutdown integration, allocation/publication, direct TX ownership and hardware
pending-work tests remain open, along with full original-firmware parity.
