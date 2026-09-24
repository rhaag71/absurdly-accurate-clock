# PPS UTC milestone

## Receiver contract and association

This implementation assumes a **1 Hz navigation solution and rising-edge 1 PPS**
aligned to whole UTC seconds. GPRMC for epoch T must follow the PPS for T in the
same second. Bench testing has acquired synchronization with RMC completion
approximately +140 ms after the preceding PPS, supporting this association.
No receiver commands are sent.

The u-blox 6 Receiver Description and Protocol Specification, sections 12.1–12.4,
documents configurable time-pulse frequency, polarity, grid, and alignment.
TIM-TP describes the *next* pulse; that UBX message's semantics must not be
applied to RMC. The RMC definition reports UTC navigation time, not UART delivery
time. The measured +140 ms phase fits the existing acquisition window; it does
not independently rule out a consistently delayed absolute label. Reference:
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

## PPS-synchronized rolling decade indicator

The clock row is UTC only. The digit after the decimal point is a visual
progression indicator, **not part of UTC and not decimal tenths of UTC**. It is
also independent of the tens-of-seconds digit. At PPS the clock arrives at
HH:MM:SS.0 and holds .0 for 500 ms. It then winds through .1–.8 in 50 ms
steps, reaching .9 at 900 ms and holding it until the next PPS.

The main loop takes an interrupt-protected PPS snapshot, polls the unchanged
timebase with that snapshot, and derives phase using unsigned elapsed
microseconds since `pulse.at_us`. Before 500000 us it returns 0; afterward it
uses `min(1 + (elapsed_us - 500000) / 50000, 9)`. There is no modulo-one-second
calculation or incremental animation timer. The existing changed-character
output sends the new authoritative second and .0 in the same boundary update,
with only the normal sequential UART command latency between them.

| Elapsed since captured PPS | Visual digit |
| --- | --- |
| 0–499.999 ms | 0 (hold) |
| 500–549.999 ms | 1 |
| 550–599.999 ms | 2 |
| 600–649.999 ms | 3 |
| 650–699.999 ms | 4 |
| 700–749.999 ms | 5 |
| 750–799.999 ms | 6 |
| 800–849.999 ms | 7 |
| 850–899.999 ms | 8 |
| 900–999.999 ms | 9 |

Only a newly captured authoritative PPS restarts at 0. If an edge is missing,
9 remains held until the existing PPS timeout invalidates the clock. When UTC
or PPS synchronization is unavailable the time reads `--:--:--` and the indicator
reads `-`. A delayed loop jumps to the correct current phase; it never catches
up by emitting intermediate digits. Existing UART-stall handling can still
invalidate synchronization independently of this effect.

Animation is read-only presentation code. It cannot advance/correct UTC, mutate
the timebase, or change RMC/PPS association. There is no animation work in the
ISR, no new delay, and no extra NMEA parser. The existing heartbeat and concise
startup/GPS/synchronization USB diagnostics remain unchanged.

## Final 20-column layout

Physical columns are one-based. Outer delimiters are not displayed. Spaces below
represent **cleared, unwritten cells**, not transmitted padding bytes:

```text
|12345678901234567890|
|UTC 03:48:03.0      |
|GPS | PPS |         |
```

| Row | Physical columns | Content |
| --- | --- | --- |
| 1 | 1–3 | UTC |
| 1 | 4 | Cleared gap |
| 1 | 5–6 | HH |
| 1 | 7 | Colon |
| 1 | 8–9 | MM |
| 1 | 10 | Colon |
| 1 | 11–12 | SS |
| 1 | 13 | Literal decimal point |
| 1 | 14 | PPS-synchronized rolling decade indicator (or `-`) |
| 1 | 15–20 | Cleared, unused |
| 2 | 1–3 | GPS |
| 2 | 4 | Cleared gap |
| 2 | 5 | `\|` if GPS data valid; otherwise `-` |
| 2 | 6 | Cleared gap |
| 2 | 7–9 | PPS |
| 2 | 10 | Cleared gap |
| 2 | 11 | `\|` if PPS present; otherwise `-` |
| 2 | 12–20 | Cleared, unused |

There is no numeric PPS counter or permanent SYNC label. Synchronization remains
internal state and in USB diagnostics. RMC does not supply satellite count, so
SAT is omitted rather than invented. Future `$GPGGA` (or appropriate talker GGA)
parsing could provide **satellites used** in field 7 (`numSV`); satellites in view
would be a different metric (GSV), not interchangeable with satellites used.
Reference: [u-blox GGA definition](https://content.u-blox.com/sites/default/files/products/documents/u-blox7-V14_ReceiverDescriptionProtocolSpec_%28GPS.G7-SW-12001%29_Public.pdf).

## PD-2200 investigation and blank-cell strategy

Bench evidence shows zero glyphs where software supplied 0x20. The earlier host
audit found no visible NUL or ASCII-zero padding, so this is **not established as
a C-buffer defect**. The exact device-side cause remains undetermined.

The Posiflex PD-2601 technical manual's Noritake section lists reset `1B 49`,
clear `0E`, brightness `1B 4C bb`, and direct position `1B 48 pp`. This is a related
Posiflex model's command reference, not proof of the PD-2200's firmware/font
mapping. The PD-2300 user manual also describes software emulation and font
selection; it identifies PD-2200 as its predecessor. Neither reviewed source
establishes a space-to-zero mapping for our unit. The actual verified PD-2200
clear/home behavior remains authoritative for this workaround. References:

- [Posiflex PD-2601 technical manual, Noritake section](https://manualzz.com/doc/6566084/posiflex-pd-2601-customer-display-technical-manual)
- [Posiflex PD-2300 user manual](https://www.manualslib.com/manual/535887/Posiflex-Pd-2300-Series.html)

No emulation, font/code-page, baud, or hardware configuration is changed. No
substitute blank glyph is guessed. The original initialization bytes remain
`1B 49`, `16`, `1B 4C 3F`, `0E`, `0C`, including the bench-approved brightness.
Clear `0E` blanks the display; `0C` explicitly homes it. After this one startup
clear, positioned short fields write only meaningful characters. Even interior
word separators are left cleared, never written as spaces. The literal period
is normal ASCII `2E`, not a special attached-decimal command.

Normal operation uses only four-byte direct-position character commands. Time
and status availability changes overwrite occupied cells with digits, `-`, or
`|`, so no erasure/space or full redraw is needed. `09 -> 10` changes both seconds
digits; `59 -> 00` also changes the necessary minute/hour digits. A new layout
must explicitly clear once and rebuild/reset the output cache. The full-row API
is retained for tests/other uses but is never called by normal clock operation.

`clock_display::Output` holds at most one direct-position command, not a queue
of animation frames. It sends at most one byte per writable UART service call.
Selection compares the latest desired frame with submitted characters; UTC
characters have priority over the indicator, followed by status flags. If a
header is partly sent when phase changes, its final glyph is refreshed from the
latest desired frame. No historical intermediate phases are retained. Bytes
already accepted by the UART cannot be recalled; visible updates have normal
9600-baud wire latency (about 4.2 ms per character command). If phase returns to the already submitted glyph after a position header has
been sent, the redundant data byte is omitted: `ESC H position` is a complete
command by itself and the next update positions explicitly again. Thus even
this case sends no unchanged glyph.

`Frame` still accounts for all 20 cells and retains ASCII-space placeholders and
a terminator at byte 20, but the operating output path never sends these spaces
or terminators. Host tests simulate a display that renders any transmitted space
as `0` and verify that clear-plus-positioned writes still produce the desired
layout. They also verify exact initialization bytes, all phase thresholds,
missed phases/backpressure, PPS resets, digit rollovers, and minimal status writes.

Before commit, bench-check the cleared gaps/trailing cells, literal period and
vertical-bar glyphs, 500 ms hold-0 followed by 50 ms progression and final hold-9, reset at PPS, no
seconds flicker, acquisition/loss transitions, and unchanged brightness. If a
cleared/unwritten cell still shows zero, compare the UART trace and actual
emulation/font settings; do not guess a different blank byte.
