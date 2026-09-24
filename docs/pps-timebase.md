# PPS UTC milestone

## Receiver contract and association

This implementation assumes a **1 Hz navigation solution and rising-edge 1 PPS**
aligned to whole UTC seconds. GPRMC for epoch T must follow the PPS for T in the
same second. This is an explicit integration assumption, not a measurement of
this particular receiver. No receiver commands are sent.

The u-blox 6 Receiver Description and Protocol Specification, sections 12.1–12.4,
documents configurable time-pulse frequency, polarity, grid, and alignment.
TIM-TP describes the *next* pulse; that UBX message's semantics must not be
applied to RMC. The RMC definition reports UTC navigation time, not UART delivery
time. Receiver model/configuration and actual RMC phase still need bench
confirmation. Reference:
https://content.u-blox.com/sites/default/files/products/documents/u-blox6-GPS-GLONASS-QZSS-V14_ReceiverDescrProtSpec_(GPS.G6-SW-12013)_Public.pdf

The ISR captures `micros()`, increments a sequence, and marks an edge seen.
The main loop takes an interrupt-protected snapshot. After observing a
900–1100 ms edge interval, RMC may label the most recent edge if:

- The existing checksum/status/time/date parser accepts GPRMC.
- The time has no nonzero fractional component (`.00` is fine).
- Its `$` is consumed at least 20 ms after the captured edge.
- The complete sentence arrives no later than 900 ms after that same edge.
- The pulse sequence is unchanged between `$` and sentence completion.

Two consecutive edge labels must differ by exactly one UTC second. The next
edge commits the second label plus one and asserts synchronization. RMC never
advances or rewrites the local epoch on receipt. Once locked, each qualified
edge increments local Unix UTC independently of RMC delivery. Coherent labels
continue to verify/reanchor it. A conflicting label drops lock; two coherent
labels are required before committing a correction at a subsequent edge.

This rejects obvious ambiguity, but **cannot detect a consistently whole-second
delayed receiver stream or wrong configured pulse phase**. Before upload/release,
review the assumption against this receiver's model/configuration and scope or
logic-analyzer trace of PPS versus GPRMC. The acquisition diagnostic reports the
last usable RMC completion offset in milliseconds; it does not independently
prove the absolute UTC label. Output outside the guarded window leaves GPS
valid but prevents acquisition. High-rate/fractional navigation is unsupported.

Startup-buffered UART input is drained before association. A main-loop servicing
gap over 20 ms discards pending association, clears lock, resets the parser, and
drains buffered input before resuming. This prevents known local backlog from
being assigned fresh consumption timestamps. Normal reception is bounded and
non-blocking. The existing VFD startup delays and GPS FIFO remain unchanged.

## State and failure behavior

`State` separates `gps_valid`, `pps_present`, `pps_locked`, and `utc_valid`.
`utc_seconds` and its calendar representation are authoritative only while
`utc_valid`; the last epoch remains stored but hidden after invalidation.

- PPS presence expires at 1500 ms without an edge; UTC becomes unavailable.
- Missing/skipped sequences or intervals outside 900–1100 ms clear lock.
- Invalid GPRMC clears GPS validity and lock immediately.
- No valid GPRMC for 3 seconds clears GPS validity and lock.
- No usable RMC/PPS association for 3 seconds clears lock even if GPS data is valid.
- Bad checksums/unrecognized sentences do not refresh GPS validity.

No oscillator extrapolation or long-term holdover is implemented. PPS edges
advance time only while GPS remains fresh. All calendar work occurs in the main
loop. Existing parser policy remains 2000–2099 with second 60 rejected; leap
second insertion/deletion support is deferred.

## Display and diagnostics

Rows are exactly 20 characters, using literal spaces for padding:

```text
|UTC 03:01:27 GPS OK |
|PPS:OK SYNC:OK      |
```

Delimiters above are not sent. Unavailable fields become `--:--:--`, `GPS N/A`,
`PPS:NO`, or `SYNC:NO` independently. No pulse count is displayed.

Status/availability transitions send full rows. Otherwise only changed time
characters use `ESC H position character`: seconds ones is zero-based column 11,
tens is 10, minutes are 7–8, hours 4–5. No unchanged digit is retransmitted.
`09 -> 10` sends two characters; `59 -> 00` also updates changed minute/hour
digits. Unix/calendar progression handles date rollover; date is not displayed.

The timebase boundary is the captured PPS, but the main-loop response plus
9600-baud UART/display latency delays visible rendering slightly. An individual
4-byte character command takes about 4.2 ms on the wire; rollover commands are
sequential, not visually atomic. This is deliberate digit updating, not a claim
of zero-latency display hardware.

USB diagnostics are queued without blocking: startup, GPS acquisition/loss,
and PPS synchronization acquisition/loss. Transitions are reported once per
transition, not every loop. The bounded queue can drop complete messages if
USB stays unavailable through many transitions. The LED heartbeat is unchanged.
