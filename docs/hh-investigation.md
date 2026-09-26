# Impossible HH investigation

> **Historical baseline, superseded in presentation and VFD details.** This
> investigation records the UTC-only renderer and single-character command
> architecture that existed when it was written. The current renderer applies
> display-zone conversion in `src/display_time.cpp` / `src/clock_display.cpp`,
> and `src/clock_vfd.cpp` uses contiguous paired-HH writes plus current
> diagnostics. The findings about authoritative UTC range and the limits of
> UART-acceptance evidence remain historical investigation results; recheck the
> linked source before treating any implementation description below as current.

Investigation of the then-current repository; production code unchanged at that
time.

## Data path

Checksum-validated GPRMC accepts hours 00..23 and dates in 2000..2099.
Timebase::receive converts qualified labels to Unix seconds. Timebase::poll alone
applies confirmed labels or increments the authoritative epoch on new PPS edges;
it derives state.utc with fromUnix. All state/render/output work is in the main
loop; the ISR only updates the separately snapshotted PPS capture.

At that baseline, render built a fresh UTC-only frame. Hour division/modulo
produced zero-padded characters at row 0 columns 7 and 8 (physical columns 8
and 9). There was no timezone conversion.
The renderer trusts utc_valid and does not independently range-check the hour.
difference compares each occupied character with Output::submitted_, including
leading zeroes. Spaces are deliberately excluded, but HH is digits or dashes.

Output scans row 0 left to right before row 1, so HH precedes the rolling cell at
column 16 and bottom-row status. There is one four-byte command buffer, not a
queue of frames. ESC H address payload is sent one byte per writable service call.
The address stays fixed through partial headers; immediately before sending the
payload it is refreshed from that SAME desired cell. If it reverted to the cached
value, only the already-complete position command remains, which needs no payload.
Failed writes retain the byte offset. Only successful payload acceptance updates
that cell's cache. Nothing in the normal loop resets this cache or writes through
a competing Serial2 sender. Startup fields precede the one reset and UART flush.

The installed Arduino-Pico SerialUART implementation checks hardware writability
and uses uart_putc_raw; write returning 1 means UART acceptance, not display
acknowledgment. RX is disabled. The physical display is never read back.

## Findings and limits

No normal-input path to hour >23 was found. fromUnix uses signed remainder and
narrows to uint8_t: fromUnix(-3600).hour is 255. This helper has no negative-epoch
validation, but accepted RMC dates and forward PPS increments cannot reach it.
A corrupted state could also pass through the unguarded renderer; no source of
such corruption was found. Neither case proves the photographed 28 or earlier 84.

Lossless byte-stream tests do not reproduce a persistent wrong HH. They include
all hour pairs, all partial-command boundaries at the seven dynamic HH/indicator/
status cells, retry/backpressure, repeated activity, and actual authoritative PPS
hour rollovers. Both complete 20-cell rows, not merely command bytes, must match.
Tests use the driver's intended ESC H protocol, not a verified physical emulator;
receiver timing, electrical corruption and physical glyph mapping remain outside
that model. Multi-cell changes are not atomic, so intermediate mixed old/new
values during transmission are possible; eventual convergence is what is tested.

Deliberately losing an accepted zero payload at column 7 on 20->08 leaves the
screen at 28:16:01.0 while the cache is correct. Subsequent status/indicator writes
leave that stale cell alone. This proves suppression can preserve physical/cache
divergence, NOT that loss happened in the reported event. A missed 23->00 leading
zero could similarly persist until the leading digit next changes. A leading 8
cannot be explained merely by retaining a previously valid tens-of-hours digit;
84 needs an additional cause such as misaddressing, corruption, or invalid state.

Strongest remaining suspects are lost/misinterpreted data or position bytes at the
receiver, or display-side address/glyph behavior. Their plausibility comes from
the transmit-only cache and the existing bench observation that space displays as
zero. That observation is not proof of a protocol mismatch. Host command replacement
and backpressure alone did not cause a failure in the tested interleavings.

## Next discriminating overnight diagnostic (proposal only)

Add a fixed-size RAM flight recorder on the Pico, downloadable by a USB command
when reconnected without resetting/power cycling. Retain all HH command starts,
accepted bytes, payload refresh/omission decisions and completions, plus hour and
validity changes. Each record should include PPS sequence/monotonic time, epoch,
calendar HH, desired/cached HH, command address/index/payload and write result.
Retain counters for blocked writes, rejected writes and dropped diagnostic records.
Use a separate small recent-all-bytes ring and a reserved HH-event history so
rolling traffic cannot evict overnight hour evidence. Avoid serial formatting or
flash writes on the timing path. Existing 512-byte USB messages drop when full and
do not record these facts. RAM retention requires keeping the Pico powered; cold
reset persistence would need a separately designed bounded flash log, not assumed
.noinit survival.

Pair the recorder with a timestamped photo/time-lapse. Correct epoch/calendar but
wrong desired HH identifies formatting/state corruption; correct desired but wrong
command identifies output corruption; correct accepted commands with wrong screen
moves the fault downstream. A logic analyzer at the display input is then the most
discriminating bench check: compare actual wire bytes with accepted-byte records.
Software logging alone cannot prove what arrived or what glyph the VFD rendered.
Do not add periodic redraws before capture: they could hide the evidence.
