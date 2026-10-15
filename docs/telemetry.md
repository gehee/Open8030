# Telemetry

The development firmware observes two kinds of downlink data from the original
transmitter: transmitter status and flight-controller MSP replies. The existing
Linux backend still receives the original application stream. Open8030 additionally
validates messages and retains selected values for a read-only host command.
This command requires the development image containing the telemetry diagnostic;
the v0.1.0 release does not provide it.

```sh
./open8030 telemetry YOUR_VRX_IP
./open8030 telemetry YOUR_VRX_IP --watch
./open8030 telemetry YOUR_VRX_IP --watch --json
```

`--watch` refreshes once per second; Ctrl-C exits. `--json` produces one JSON
object, or JSON lines when watching. Use the same receiver address as `run`.
The command uses daemon TCP port 50000; it does not need SSH or subscribe to
payload sockets. It can report a disconnected radio without starting it.

## Readings

| Record | Values | Source |
| --- | --- | --- |
| Transmitter | Supply voltage in V, temperature in °C | Application type 5 |
| FC variant | Four-character firmware identifier | MSP command 2 |
| FC version | Three version bytes displayed as major.minor.patch | MSP command 3 |
| FC status | Cycle time in µs, I²C error count, sensor mask, mode flags, PID profile | MSP command 101 |
| FC analog | Battery voltage in V, consumed mAh, current in A, raw RSSI | MSP command 110 |

Transmitter voltage and FC battery voltage are separate reports and need not be
equal. These are received readings, not independently calibrated measurements.
Zero is a valid reported value; it does not prove that the corresponding sensor
is fitted or configured. RSSI is left in the FC's raw units. Mode flags are left
as a bitmask because their names require the FC's active box mapping; the command
does not infer armed state from them. Additional MSP messages continue through
the host stream even when this readout has no decoder for them.

Each record has its own message count and age since the last valid reception:

- `current`: received during this link session, radio linked, age at most five seconds.
- `stale`: an older saved value, shown explicitly as stale rather than refreshed by unrelated traffic.
- `missing`: no valid record in this link session; JSON values are `null`.

The five-second threshold is a display policy, not a promised transmission rate.
Identity/version records can become stale if the transmitter sends them rarely.
Age measures reception at the radio processor, not sensor sampling time. Link
changes clear records and partial messages. A transport error terminates the
command rather than continuing to display an apparently live result.

## Data path and validation

In the tested mode, MAC port 3 maps to host socket 2. Its application stream has
an `fe a5` header, a header checksum, a 12-bit payload length, a channel/ACK field,
an opaque trailer byte and a CRC32C. The CRC covers the header, payload and trailer;
it uses reflected polynomial `0x82f63b78`, initial zero and no final complement.
Maximum envelope length is 4106 bytes. The observer reassembles envelopes across
MAC payload boundaries and processes concatenated envelopes. Only non-ACK
channel-0 messages update the selected telemetry records.

Application type 5 contains transmitter status. The matched original backend's
consumers identify byte 1 as signed temperature in °C and bytes 11–14 as a
little-endian supply reading in mV. Other fields remain uninterpreted here.
Application type 2 carries FC bytes. The observer validates MSPv1 XOR checksums,
native MSPv2 CRC8-DVB-S2 checksums and MSPv2 encapsulated inside MSPv1, accepting
reply direction `>`. MSPv2 payloads are bounded to 512 bytes. MSPv1 jumbo frames
are not decoded. Known records need their minimum field lengths; malformed or
short messages never replace a valid reading. Incomplete input expires after a
two-second gap on that parser's input; link changes discard it immediately.

MSP field layouts follow the public
[Betaflight MSP reference](https://betaflight.com/docs/development/MSP-Protocol-Reference-Dev),
[message definitions](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp.c)
and [MSP framing implementation](https://github.com/betaflight/betaflight/blob/master/src/main/msp/msp_serial.c).
The implementation here is independently written; no Betaflight code is bundled.

The diagnostic snapshot (`0x0c5a002c`, daemon alias `0x015a002c`) is 444 bytes:
16 little-endian 32-bit header words, then five 76-byte records in the table's
order. Each record is update time, count, stored length (three LE32 words) and a
zero-padded 64-byte prefix. Header fields and counters are defined in
[ar_host_telemetry.h](../include/ar_host_telemetry.h). Times use the local hardware
millisecond counter modulo 2³², independent of AP synchronization. Counters wrap
modulo 2³²; ages spanning a full counter period are not meaningful.

## Scope

This is passive downlink observation. It does not send MSP requests, configure the
flight controller, provide RC uplink or implement RC failsafe behavior. MAVLink,
SBUS and physical UART routing are not established by these MSP observations.

A separate recovered C8 path exists on MAC port 2 and can publish host event 8.
No C8 traffic was observed in this setup; its synthetic tests do not establish
live UART telemetry. Flight-controller MSP traffic in this setup arrives on
MAC port 3 instead. See [validation status](status.md) for live test results and
[wireless protocol](wireless-protocol.md) for the radio layers underneath.
