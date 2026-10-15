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
| Host getters | Main status and DEV AP-time getter | Complete field semantics, all remaining getters, unavailable states and original error responses. |
| Host setters and configuration | Candidate list and restricted loader configuration | Full dispatcher inventory, dynamic power, channel/band policies, reset/default behavior and runtime configuration changes. |
| Host lifecycle | Explicit stop/start and stop/deinit/init/start tested historically | Every wrong-state/repeated operation, cancellation, pending DMA, failure recovery and complete reset semantics. |
| Events | Subscription and delivery for IDs 0 and 1 | Other original IDs, payload layouts, ordering, cancellation and overflow behavior. |
| Host sockets | Slot-0 copy-mode data path; normal backend video/control | Other modes/slots, retained-descriptor TX, backpressure, teardown races and original capacity/error behavior. |
| MAC framing | Header/descriptor parser and DEV encoder | Other formats/flags, receive ordering, duplicate handling and all original rejection rules. |
| AP full/compact controls | Selected mode-0/slot-0 admission and connected control | All extensions, remote synchronization effects, changed channels, slots, stale/duplicate controls and unsupported modes. |
| Internal port-0 commands | Stream parsing; login checksum ACK transmission | Receiving a command currently mainly records it. Inventory and implement each original command's effects, responses and retries. |
| PHY commands | See command table below | Exact callbacks, TTL/replacement ordering and side effects for every command. |
| MCS adaptation | RX adaptation and limited TX increase | TX decrease/interleaver drain, all PHY keys, retry/expiry, loss interactions and sustained adaptation. |
| Power and calibration | Initial user-0 setup | Normal backend startup power writes, all users, peer-driven power, compensation and calibration effects. Earlier dynamic-power candidates broke acquisition. |
| Channel/band selection | Configured search and matched channel | Independent uplink changes, subchannels, hopping, scan scoring, automatic policies, rollback and cross-band operation. |
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
| Getters (`0x01…`) | 35 | Status and DEV AP time have partial behavior/scenario coverage; 33 unsupported |
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
| 0 | Cache/observe | Original no-op/cache semantics |
| 1 | Unsupported | Original callback and hardware effects |
| 2 | Unsupported | Original callback and hardware effects |
| 3 | Limited TX-MCS update; outgoing RX-MCS request | Decrease/drain and full timing/acknowledgement behavior |
| 4 | Unsupported | Channel transition, confirmation and rollback |
| 5 | Unsupported | Original callback and hardware effects |
| 6 | Selected RX/TX identity fields | All user/state combinations and shared-field effects |
| 7 | DEV AP-time offset and host getter | Physical peer-loss/stale time, long-run hardware wrap and other roles/modes |
| 8 | Validated format only | Original callback and hardware effects |
| 9 | Validated format only | Original callback and hardware effects |

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

Next, extend the core inventory through the transport dispatchers and indirect
callbacks, and bind each command's behavior to its comparison tests. Then cover
radio messages, timers and state transitions in the same way. Dynamic power still
needs a separately isolated acquisition fix. Neither a large synthetic test count
nor a visible scene can close the remaining matrix by itself.
