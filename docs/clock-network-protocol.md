# Clock Network Protocol v1

This is the Pico-side clock-source interface, independent of displays and the
future ESP32 implementation. The source owns UTC; the controller is a read-only
consumer. MOSI carries dummy bytes, never commands. No network observation can
set or discipline the source clock. No holdover is implemented.

## Electrical and transaction contract

3.3 V logic only, common GND, short wiring. RP2350 is native SPI1 peripheral;
ESP32 is controller. GPIO numbers are not physical header numbers.

| Pico GPIO | Header pin | Direction at Pico | Signal |
| --- | --- | --- | --- |
| GP8 | 11 | input | SPI1 RX / controller MOSI |
| GP9 | 12 | input | SPI1 CSn, active low |
| GP10 | 14 | input | SPI1 SCK |
| GP11 | 15 | output | SPI1 TX / controller MISO |
| GP12 | 16 | output | TIME_SYNC |
| GP13 | 17 | reserved | future control/IRQ, not initialized |

This corrects the earlier wiring draft's controller-oriented MISO/MOSI labels.
TX/RX directions do not swap in peripheral mode. GP9 has an internal pull-up;
SCK/MOSI have pull-downs for an absent controller. TIME_SYNC starts low. MISO
is hardware-tristated when CS is high.
Do not drive unpowered boards through their IO; arrange shared power sequencing
or isolation where independent power is needed.

SPI **mode 1** (CPOL=0, CPHA=1), MSB first, 8-bit words. Start at **100 kHz**;
v1 Pico implementation supports at most 100 kHz pending bench qualification.
Mode 1 is intentional: PL022 SPH=1 permits continuous frames under one CS.

Controller procedure:
1. Allow 1 second after Pico startup before the first request. Keep CS high at
   least 1 ms between transactions; at most 10 transactions/second.
2. Assert CS low, wait **at least 100 us** for the bounded CS interrupt to reset,
   select an immutable snapshot, and preload the FIFO. Do not clock earlier.
3. Clock exactly **40 bytes** continuously, sending zeroes on MOSI.
4. Hold CS low at least **10 us after the last trailing SCK edge**, then deassert it. Validate signature/version/length,
   reserved fields and CRC before using any data. Reject incomplete reads.

CS falling latches the latest fully published packet. It cannot change during
that transaction. Short/extra/aborted transactions never affect the clock. Each
new CS resets SPI1, discarding old FIFO/partial-word state. Extra complete bytes
are counted as malformed; sub-byte tails cannot always be counted by this SPI
peripheral. Stop after 40 bytes: surplus output is unspecified, not another packet.
After 48 received bytes transport interrupts are disabled until CS rises, bounding
service for a stuck/overclocking controller. A CS held low without clocks does not
block the main loop. No busy waits on controller activity, DMA, or PIO are used.

## Packet layout (40 bytes)

Multi-byte integers are **little endian**. Explicit byte serialization, no packed
C structures. Signed epoch uses 64-bit two's complement Unix seconds, ignoring
leap seconds as the source timebase does. Values beyond 2038 are supported.

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `ACT1` (41 43 54 31) |
| 4 | 1 | version | 1 |
| 5 | 1 | length | 40 |
| 6 | 2 | flags | below |
| 8 | 4 | packet_sequence | increments modulo 2^32 for each new published snapshot; first is 1 |
| 12 | 4 | boundary_sequence | source boundary counter (Pico PPS capture sequence); 0 before any capture; wraps modulo 2^32 |
| 16 | 8 | utc_seconds | authoritative UTC second beginning at that boundary; 0 when UTC_VALID clear |
| 24 | 4 | sync_sequence | count of TIME_SYNC rising edges this boot, modulo 2^32 |
| 28 | 4 | sync_delay_us | TIME_SYNC edge offset after captured boundary; FFFFFFFF if SYNC_VALID clear |
| 32 | 1 | satellites | satellites used, 0..99; FF when SAT_VALID clear |
| 33 | 3 | reserved | zero |
| 36 | 4 | crc32 | CRC of bytes 0..35, little endian |

Flags: bit 0 GPS_VALID; bit 1 PPS_PRESENT; bit 2 PPS_LOCKED; bit 3 UTC_VALID;
bit 4 HOLDOVER (reserved, always clear today); bit 5 SYNC_VALID;
bit 6 SAT_VALID. Bits 7..15 zero. GPS/PPS/UTC bits directly reflect one snapshot of
the existing authoritative State. GPS_VALID alone is NOT valid UTC. Neither
PPS_PRESENT nor UTC_VALID alone establishes a matching TIME_SYNC edge. Satellite
status is informational. A different clock provider may have GPS_VALID clear
while supplying a valid, locked authoritative 1-Hz boundary; GPS_VALID is not a
consumer prerequisite for UTC. Flags can change without a new epoch/edge.

CRC-32/ISO-HDLC: width 32, reflected polynomial ED B8 83 20 (normal 04 C1 1D B7),
init FFFFFFFF, reflected input/output, final XOR FFFFFFFF. Check value for ASCII
`123456789` is CBF43926. CRC detects corruption, not authentication.

## TIME_SYNC: boundary identity and measured delivery delay

GP12 rises once for a newly processed authoritative PPS boundary only when UTC
is valid, PPS present/locked, and processing age is at most **5000 us**. It returns
low at the first main-loop service at least 100 us later; pulse width is not a
precision quantity. Invalid UTC produces no new rising edges. Existing RMC/PPS
association remains the sole authority; RMC arrival never triggers this signal.
Late processing suppresses that edge rather than emitting a misleading late pulse.
The publisher commits boundary/epoch identity, delay and emitted-edge sequence
only after the final deadline check and GPIO rising write succeed. If the final
timer read crosses 5000 us, the edge is suppressed, the skipped count increases,
and the newly published authoritative boundary has SYNC_VALID clear and
sync_delay_us set to FFFFFFFF. `sync_sequence` remains the cumulative count of
edges actually emitted; it does not imply that the current boundary was emitted.

The rising edge is a software-delivered boundary reference, NOT a packet-ready
strobe and NOT a zero-latency electrical copy of GPS PPS. The packet epoch identifies
the PPS boundary; `sync_delay_us` measures the elapsed time from the existing PPS
capture timestamp to a timer read immediately before the GPIO rising write. The
consumer estimates that boundary at captured_TIME_SYNC - sync_delay_us. This
retains source PPS ISR timestamp latency, 1-us timer quantization and the short
read-to-GPIO-write delay; it does NOT establish calibrated sub-microsecond accuracy.
A requirement for an exact hardware-phase copy needs a separate timing design.

After a TIME_SYNC edge, wait **1 ms** before asserting CS to allow packet publication.
Check SYNC_VALID, UTC_VALID, PPS_LOCKED and PPS_PRESENT, then associate
sync_sequence and boundary_sequence with the captured edge. Read again if the
packet is still old. The first observed edge/packet establishes the consumer's
sequence alignment. Missed edges, sequence discontinuity, reboot, invalid flags,
or no edge for 1.5 seconds invalidate that phase association. Repeated SPI reads
are NOT new seconds. At boot the consumer discards any prior association.
Do not associate a packet fetched across the next TIME_SYNC edge with the wrong
edge: timestamp/count both edges and CS start, or discard/retry ambiguous reads.

SYNC_VALID remains set only for the current valid boundary and matching epoch
for which a pulse was emitted. On invalidation or a suppressed edge the packet
clears it and sets sync_delay_us to FFFFFFFF; sync_sequence retains its count.
An invalid packet's
zero epoch is a sentinel, not 1970 time. On recovery only a subsequent qualified
boundary emits a pulse. Every new PPS sequence or quality/count/epoch change
publishes a new coherent packet; no SPI read increments packet_sequence.

## Implementation and compatibility

Main-loop code reads State/Pulse only after Timebase::poll of that same capture.
It encodes into the inactive packet buffer then publishes its index with release
ordering. The same-core CS ISR acquires and copies a complete packet into its
transaction buffer; subsequent publications cannot alter it. Interrupt work is
bounded (40-byte copy/reset/preload, or at most eight RX/TX FIFO accesses per
SPI interrupt). IRQ handlers never parse NMEA, convert UTC, format USB text or
modify the timebase. Arduino-Pico's SPISlave streaming callbacks lack the needed
CS framing, so transport uses the bundled SDK SPI/GPIO/IRQ/reset APIs directly.

USB: one NET init message; one bounded NET summary per 60 seconds containing
latest packet sequence/epoch/flags, transaction/short/extra/overrun counts,
TIME_SYNC edge/skipped counts. Existing USB queue/drop policy applies. No default
per-second output. All resources remain local to the Pico repository.

Unknown protocol versions/lengths/reserved flags are rejected. An incompatible
layout or semantics requires a new version; reserved bytes/bits are not silently
repurposed in v1. Future HOLDOVER needs a specified uncertainty/age model and
consumer policy before activation. GP13 has no role in v1.

Hardware references: RP2350 datasheet sections 1.2.3 and 12.3 (mode 1 continuous
frames, fixed TX/RX directions, peripheral CS tristate); installed Arduino-Pico
SPISlave and Pico SDK hardware_spi sources. Bench validation with an actual SPI
controller is required for CS setup/hold, byte alignment after aborts, electrical
levels, and TIME_SYNC offset/jitter. Host tests cannot certify these timings.


## Golden vectors and validation

`test/host/network-v1-vectors.json` contains portable hex vectors generated with
Python `struct.pack` and `zlib.crc32`, independently of the C++ encoder. The host
suite checks those fixed bytes, every single-bit corruption in a packet,
header/reserved-field rejection with recomputed CRC, epochs through INT64_MAX,
invalid/GPS-only/PPS-unlocked states, sequence progression and immutable copies.
Actual transport code is also compiled against a native-SPI register model:
all 41 publication split points, all short byte counts, extra bytes, stalled CS,
TIME_SYNC gating, and low-volume diagnostic counters. This is not a replacement
for electrical SPI/TIME_SYNC bench validation.

Hardware references:
- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
  sections 1.2.3, 12.3.1 and 12.3.4.11.
- [Pico SDK SPI APIs](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_spi).
- Installed Arduino-Pico 1.60100.0+sha.fd65f6d4, `libraries/SPISlave/src` and
  `pico-sdk/src/rp2_common/hardware_spi` were inspected before implementation.
