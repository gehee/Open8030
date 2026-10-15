# Configuration and parameters

The supplied workflow reproduces one verified Ascent VRX configuration. It checks
image, board-profile and receiver JSON hashes; it is not currently a general
radio-tuning tool. The ranges below describe parser support, not hardware
validation of every value. Other receiver variants and VTX/AP operation remain
unverified.

## Configuration sources

| Source | Purpose | When it is used |
| --- | --- | --- |
| Private `.local/board-profile.c` | Ordered register defaults, pin data, frame-control-header encoding keys and flash-part descriptions | Compiled into the hardware runtime |
| Receiver `/usrdata/fpv/boot_ar8030/bb_config_gnd.json` | Role, channel list, identities, power calibration and MCS policy | Copied by `run`; supplied to RAM by the original loader |
| Hardware OTP and factory storage | Board identity and available power offsets | Read during runtime boot |
| Original Linux backend | Candidate peers, lifecycle commands, sockets and selected runtime controls | After daemon/backend startup |

The private profile defines `ar_boot_embedded_profile` from
[ar_boot.h](../include/ar_boot.h). The matched board path requires 704 ordered
register-default entries (5,632 bytes), 40 bytes of pin data and 100 bytes of
encoding keys. The loader-based path uses a null `plan` pointer and constructs its
radio plan from JSON; a non-null `plan` selects a separate compiled-plan path.
Neither path supplies a generic profile for arbitrary boards.

The profile is not distributed and there is no public generation procedure yet.
`--profile` accepts a matching `.c` file inside `.local/`, not arbitrary replacement
board data. QEMU and native tests do not require it. Synthetic fixtures in
[tests/config_fixture.h](../tests/config_fixture.h) exercise parsing; they are not
a deployable receiver configuration.

## Build and CLI parameters

| Command or option | Meaning |
| --- | --- |
| `./open8030 build` | Build runtime and bootstrap, package and check the recorded baseline |
| `build --prepare` | Also download/install the pinned build dependencies |
| `build --profile .local/FILE.c` | Select the private profile; default is `.local/board-profile.c` |
| `run HOST` | Upload, start, check identity/link and keep the RAM session running |
| `status HOST` | Query replacement identity and RF link; no camera inspection |
| `stop HOST` | Restore the original radio/video services for the saved session |
| `test` | Run native C suites and Python helper tests locally |
| `qemu [--prepare]` | Build and test the UART protocol in QEMU |

`HOST` is an IPv4 address or hostname, without `root@`, a port suffix or shell
syntax. The helper uses root SSH and daemon TCP port 50000. Build and test commands
do not need a receiver address. There are no public CLI options for radio power,
channel, board ID or rollback duration.

[toolchain.json](../toolchain.json) pins the Zephyr revision and SDK archives;
[requirements-build.txt](../requirements-build.txt) pins Python build packages.
[prj.conf](../prj.conf) and [boards/ar8030_bringup.conf](../boards/ar8030_bringup.conf)
configure the runtime: console/log output is disabled, the main stack is 4,096
bytes, and the hardware build uses polling. Changing these can change the binary
hash and invalidate the accepted build.

## Loader JSON: fixed supported modes

Paths in this section are relative to `baseband.basic`. String values are exact,
including numeric-looking strings such as `"5"`. The parser rejects unsupported
modes rather than silently substituting the matched board settings.

| Path | Required value |
| --- | --- |
| `role` | `"dev"` |
| `dev.clock` | `"200MHz"` |
| `channel.band_mode`, `channel.init_band` | `"band_single"`, `"5G"` |
| `channel.mode`, `channel.band_hop` | `"manu"`, `"manu"` |
| `channel.fs_bw` | `"5"` |
| `power.mode` | `"closeloop"` |
| `power.pwr_auto`, `power.link_auto` | `"manu"`, `"manu"` |
| `lna.inner.mode`, `lna.fem.mode` | `"manu"` |
| `lna.inner.manu`, `lna.fem.manu` | `"on"` |
| `channel.multi_mode.enable`, `channel.rc_hop.enable` | `false` |
| `dev.mcs.slot.bw_auto.enable` | `false` |

The four PHY profiles also have fixed requirements:

| Profile under `para` | `bandwidth` | Mode field and value | `tintlv_num` |
| --- | --- | --- | --- |
| `br.tx` | `"5"` | `tx_mode: "2TX"` | 2 |
| `br.rx` | `"5"` | `rx_mode: "2T2R_STBC"` | 2 |
| `slot.tx` | `"2.5"` | `tx_mode: "2TX_STBC"` | 1 |
| `slot.rx` | `"2.5"` | `rx_mode: "2T2R_STBC"` | 1 |

Each profile requires `payload: true`, `enable_tintlv: true`, `tintlv_len: 12`
and `fch_info_len: 96`. These are the implemented profile contracts, not automatic
bandwidth negotiation.

## Channels and identity

| Path under `baseband.basic` | Accepted input | Effect |
| --- | --- | --- |
| `channel.freq` | 1–42 distinct integer frequencies in kHz, greater than 4,300,000 and at most 7,500,000 | Ordered channel list; each entry must also pass PLL validation |
| `channel.init_chan` | `-1` or a zero-based index into `freq` | `-1` enables configured channel search; an index selects a fixed initial channel |
| `dev.mac`, `dev.ap_mac` | Exactly eight hexadecimal characters each | Four-byte radio identities, not six-byte Ethernet MAC addresses |
| `para.slot.tx.retx_count` | Integer 0–127 | Initial slot transmit retry setting |
| `dist_calc.enable` | Boolean | Controls the implemented synchronization-MMIO path; does not establish distance/telemetry parity |

When available, OTP replaces the local JSON identity. Missing OTP identity permits
the JSON fallback; other identity-read errors fail boot. The original backend
supplies candidate peers; a configured peer identity alone does not replace that
candidate-control step. Frequency parser bounds do not establish usable tuning
range on every board.

## MCS policy

MCS means modulation and coding scheme. Paths here are relative to
`baseband.basic.dev.mcs.slot`. The receive policy is implemented in
[dev_mcs.c](../src/dev_mcs.c); bandwidth changes and broader transmit policy are
separate work. See [MCS negotiation](wireless-protocol.md#mcs-negotiation) for the
wireless request and acknowledgement sequence.

| Field | Accepted input | Meaning |
| --- | --- | --- |
| `enable` | Boolean | Enable the receive policy |
| `mode` | `"auto"` or `"manu"` | Automatic or manual selection policy |
| `init` | Integer -2–23 | Initial selector; translated to internal index by adding two; internal 25 is the no-selection sentinel |
| `hold_time` | Integer 0–65,535 | Policy hold interval in milliseconds |
| `delay_start` | Optional integer 0–65,535; default 0 | Delay in eligible sample calls, not milliseconds |
| `max_wait_time` | Integer 0–65,535 | Pending-change wait interval in milliseconds; 65,535 disables the timeout-triggered RX reset |
| `mcs_tab` | 1–10 rows, strictly increasing `mcs` | Policy thresholds for the supported fixed bandwidth |

Each `mcs_tab` row contains:

| Field | Accepted input |
| --- | --- |
| `mcs` | Integer -2–10; converted to internal index by adding two |
| `ldpc_up_num`, `ldpc_dw_num` | Integer 0–255 |
| `ldpc_dw_conti_num` | Integer -128–127 |
| `snr_up`, `snr_dw` | Integer 0–65,535, in the implementation's raw SNR scale |
| `up_keep_time`, `dw_keep_time` | Integer 0–65,535 milliseconds |

Do not interpret the raw SNR thresholds as calibrated dB measurements. The policy
collects an eight-sample window; a sample gap greater than 200 ms discards window
readiness. If `init` matches no configured row, initial selection uses the window's
minimum SNR and the rows' downgrade thresholds. Configuring a value accepted by
the parser does not prove that the peer or hardware supports the resulting mode.

## Power and factory data

`baseband.basic.power.manu_init` accepts 9–36. Calibration rows live under
`baseband.power_calibration.default.closeloop`, with keys `9dbm` through `39dbm`.
Each supplied row has four hexadecimal byte strings: gain/amplitude for each of
two paths. Gain entries are limited to 0–63; amplitude entries to 0–255. Rows can
be sparse, but every row needed for initial and offset-adjusted selection must
exist. These names and indices are not a measurement of emitted RF power.

Optional `baseband.basic.power.pwr_offset` contains exactly four integers in
-6–6. Optional `csma_offset` is 0–255, defaulting to 8. The current boot path always
selects the `default` calibration table; although the calibration helper can select
a named board member, loader integration does not supply a board selector.

The boot path reads factory data before resolving power. Recognized version-3/4
records take precedence over JSON offsets; permitted fallback uses JSON offsets,
then zero offsets. The two implemented paths use offset elements 0 and 2, and
clamp selected row levels to 9–39. Unsupported flash identification or controller
setup is a boot failure, not evidence that factory calibration is absent.

Hardware acceptance uses initial level **25** and the zero-offset storage fallback.
A configuration-only test of the same accepted firmware with `manu_init: 11`
failed acquisition and delivered no video; restoring 25 restored the stream.
Parser acceptance of 9–36 does not establish working RF at those levels.
Dynamic host power controls and valid factory-record parity remain incomplete.
See [power_config.c](../src/power_config.c),
[power.c](../src/power.c) and [status](status.md).

## Changing configuration

`run` checks the receiver JSON against the accepted hash. Editing it therefore
causes preflight rejection even if the parser supports the edited fields.
Likewise, `build` rejects a profile, runtime or bootstrap that differs from the
recorded baseline. Replacing hashes alone does not validate a new configuration.

For development, validate parser/state-machine changes offline, preserve matched
board/release provenance, then perform a separately authorized hardware test with
stock comparison and direct video capture before accepting new hashes. Keep
private profiles, generated images and captures in ignored `.local/`.

## Telemetry readout

`./open8030 telemetry HOST` reads the current development firmware snapshot.
`--watch` refreshes once per second and `--json` selects machine-readable output.
The five-second freshness threshold applies separately to each record. The command
does not configure the transmitter or FC, and requires no additional board-profile
settings. See [telemetry](telemetry.md) for supported fields and missing/stale behavior.
