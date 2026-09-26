# HH ones zero investigation

> **Historical investigation with a current implementation addendum.** The byte
> audit and “no accommodation” conclusion below describe the pre-accommodation
> source and preserve what was known then. The current implementation uses the
> paired-HH accommodation documented at the end and in
> [`pps-timebase.md`](pps-timebase.md#pd-2200-investigation-and-blank-cell-strategy).
> It also converts UTC to the selected display zone; the historical UTC-only
> path is no longer current. Recheck current source and tests before using the
> historical serializer, diagnostic capacity, or timezone statements as facts
> about the present firmware.

Follow-up to the earlier HH investigation and timezone UI implementation. Inputs
are the reported physical 00->08 and 10->18 observations and supplied USB log
excerpts; no electrical capture or physical device is available to these tests.
No production code, diagnostics, authoritative UTC, or VFD policy changed here.
The earlier 28 example is not reclassified as a proven 20->28 event.

## Strongest finding

At the investigated baseline, no normal firmware transformation of address 08
plus payload 30 into 38 was found or reproduced. The new evidence points beyond
a simple stale leading digit:
when a physical dash has actually replaced the ones cell, merely dropping the
subsequent zero leaves a dash, not an eight. A log of '-' acceptance does not prove
that the physical cell was a dash, however. Both zeroes being accepted does not
prove delivery, interpretation or exclude a later misaddressed overwrite.

30 XOR 38 is 08: one data bit, D3, differs. This is a discriminating observation,
not proof of electrical corruption. A physical glyph resembling 8 need not mean
the receiver decoded ASCII 38. A hypothetical OR of payload with address 08 also
yields 38, but no such operation exists in the inspected code. A blanket D3-high
or address-OR rule predicts 31->39 (1->9), 32->3A (2->colon), etc.; correct x1
cases argue against that unconditional rule. An intermittent/context-dependent
fault remains possible.

## Exact source path inspected

- `main.cpp`: coherent PPS snapshot, Timebase poll/receive ordering, invalidation,
  renderer call, one Output service per loop, startup writers, USB queue and
  HH/TXHH diagnostic formatting. ISR only captures PPS; no display access.
- `clock_state.cpp`, `nmea_rmc.cpp`, `display_time.cpp`: epoch/calendar and
  presentation conversion. No new UTC or timezone defect was identified.
- `clock_display.cpp`: full initialized 2x21-byte frame, hour division/modulo,
  cells 7/8, validity placeholders, changing zone labels, non-space differences.
- `clock_vfd.cpp/.hpp`: startup fields, cache/reset, encoder sink, four-byte
  buffer, pending row/column, per-byte retry, payload refresh/omission, event.
- `pd2200.cpp/.hpp`: visibleByte, writeChar, writeField, position, writeRow,
  initialization. Bounds and synchronous copying of stack-local command arrays.
- `hardware.cpp`/`pins.hpp`: UART1/Serial2, GP4 TX, RX disabled, 9600 8N1,
  no CTS/RTS configured. Other reserved pins are not display writers.
- NMEA/GGA buffer bounds, satellite count bounds, and button enum cycling were
  also inspected for an obvious nearby corruption source; none was found.
- Installed Arduino-Pico `ArduinoCore-API/api/Print.cpp` bulk write calls the
  single-byte virtual write synchronously. `cores/rp2040/SerialUART.cpp`
  availableForWrite checks UART TX writability, and write(uint8_t) calls
  uart_putc_raw then returns 1. Pico SDK `hardware/uart.h` synchronously writes
  each byte to UART DR after waiting for FIFO space. There is no deferred pointer
  into a caller's stack, DMA buffer reuse or character translation on this path.

The byte address is row*20+column, independently stored in command_[2]. The
printable character occupies command_[3]. At each new selection size_ is zeroed
and all four bytes are copied; bounds limit it to four. During partial commands,
row/column remain fixed, and only command_[3] can refresh. No bitwise OR/addition
combines address and payload. 08 is not a string terminator; ASCII zero 30 is not
NUL. The new desired Frame is never retained by pointer after service returns.

Writable=false emits nothing. A rejected write retains next_; accepted bytes
advance next_ once. No selection occurs in the middle of a header. If the payload
reverts to the cached value, the completed position command has no payload and
the next command explicitly positions again. This conforms to the reviewed
Noritake reference, although an undocumented old-device behavior is not excluded.

All runtime output shares this one serializer. Old accepted commands can still
be in the hardware FIFO, but FIFO order is preserved; indicator/status bytes
cannot be inserted between another command's address and payload. An in-flight
indicator/status command can finish before a newly dirty HH command. RX input
and PPS work can delay submissions, not insert VFD bytes. UART writability says
nothing about the display receiver being ready; there is no receiver feedback.

## PPS loss/reacquisition

Loss changes HH and other time digits to dashes and the rolling indicator to '-';
non-UTC abbreviations may also become e.g. C--. Reacquisition changes label, all
six time digits and indicator, sometimes GPS/PPS/SAT too. These are a burst of
ordinary dirty-cell commands, not a special VFD reset/init operation. A previous
in-flight command is completed/refreshed/omitted before selecting the next one.
The scan orders label, HH, MM, SS, decade, then bottom row. Typical restoration
can involve about a dozen four-byte commands (tens of milliseconds at 9600 8N1).
Thus reacquisition repeats the suspect command and changes burst/gap timing. It
could expose or repair a downstream fault without causing a UTC defect.

## Byte comparison

The tests isolate an HH pair previously '--'; other cells are unchanged:

| Intended | Accepted bytes (hex) |
| --- | --- |
| 00 | 1B 48 07 30 1B 48 08 30 |
| 01 | 1B 48 07 30 1B 48 08 31 |
| 10 | 1B 48 07 31 1B 48 08 30 |
| 11 | 1B 48 07 31 1B 48 08 31 |
| 20 | 1B 48 07 32 1B 48 08 30 |
| 21 | 1B 48 07 32 1B 48 08 31 |

Only the final payload differs within each x0/x1 pair. 01->00 emits only
1B 48 08 30 when the other cells are unchanged. No 38 appears in that sequence.
38 may legitimately appear in other commands (indicator, seconds, satellite
count); that matters if the device loses command/address state downstream.

## Manuals reviewed

`Paper-Documents/Posiflex-display.pdf` is actually the related PD-2601 technical
manual. Noritake section, printed pages 3-34, 3-36, 3-40/41, defines digit select
1B 48 pp, addresses 00..27, and standalone 08 as backspace. The parameter 08 must
be consumed as an address after ESC H; no documented nibble combination, special
zero glyph, or mandatory gap for that sequence was found. The digit-select pages
and backspace page were visually checked as well as text-extracted. User-defined
fonts require ESC C and explicit bitmap bytes; normal runtime never emits that
command. A global font definition also poorly explains one-cell specificity.

`poledisplayPD2200.pdf` confirms Noritake mode selection (SW1 off, SW2 on; change
only powered off), but is not a detailed command reference. Its section IV.C
notes the PD2200A pass-through terminator requirement and that PD2200B handles
handshaking automatically. Exact model suffix/terminator is a bench check, not a
proven reason for 0->8. `PD1200-PoleDisplay-Programmingguide.pdf` describes another
model/protocol (DLE positioning), not authority for replacing this driver's ESC H.

The existing repository documents the unexplained space->zero observation.
That reinforces the need to validate the actual unit, but is not evidence that
this new symptom has the same cause. A web search found the same related-model
manufacturer reference, not a documented PD-2200 08/30 quirk:
https://manualzz.com/doc/6566084/posiflex-pd-2601-customer-display-technical-manual

## Tests and limits

Added `test/host/hh_zero_test.cpp` to the host runner:

- Exact byte assertions for all six requested pairs and 01->00.
- 1,008 cases: six hours, fourteen dynamic cells (including zone labels), four
  partial-command boundaries, FIFO capacities 1/4/32. Rendered loss to '--',
  reacquisition with concurrent rolling/status changes, stalls and write refusal.
- 300 further zone loss/reacquisition cycles reproducing the intended civil
  conditions UTC05/CDT00, UTC06/MDT00, UTC17/PDT10.
- 500,000 deterministic seeded service calls with zone/validity/time/status
  changes, rejection, FIFO latency, and periodic full-screen convergence checks.
- After EVERY service call, independently decode accepted bytes and compare the
  whole cache; accepted-character events must match the actual accepted payload
  and current desired cell. Final physical decoder rows must both equal target.
- Explicit downstream fault injections: losing a zero over '-' leaves '-';
  forcing D3 high yields x8 with cache x0, and also predicts x1->x9. These assert
  the injected behavior and are not claims of natural firmware reproduction.

All five host executables pass, including the previous HH and timezone tests.
The new test also passes AddressSanitizer/UndefinedBehaviorSanitizer (see run
result). The decoders implement the documented byte protocol, not receiver
analog behavior, undocumented timing, or VFD pixel electronics. No software test
can establish what this physical unit received or displayed.

## Next evidence and conditional fix

1. Capture a failing event on Pico GP4 and at the VFD input using an RS-232-rated
   probe/receiver for the latter (do not connect an ordinary logic input directly
   to RS-232 voltage). Decode 9600 8N1; retain raw waveforms, inter-byte gaps and
   commands before AND after the zero, correlated with USB records/photo.
   GP4=30 but display input=38 points to transport/transceiver/cabling. Clean 30
   at the input with no later misaddressed write points into display reception,
   state or glyph circuitry. Check the D3 sample as well as framing/levels.
2. A controlled, separate bench A/B: existing single-cell `1B 48 08 30` versus
   contiguous pair `1B 48 07 30 30` (00) or `1B 48 07 31 30` (10). The pair avoids
   an explicit 08 address. Test x1 controls, zero at adjacent cells, gaps/bursts,
   and repetition with/without rolling/status traffic. One change per trial.
3. If the wire is clean and the error follows physical cell 8 rather than the
   positioning sequence, examine the actual zero/eight dot shapes and compare
   static writes at different cells, with a cold display power cycle and verified
   mode/model settings. A glyph/drive fault is different from ASCII corruption.

**Superseded historical conclusion:** There was NO proven fix at the time. If
A/B establishes that avoiding direct address 08 reliably solves a receiver
quirk, a narrowly documented PD-2200 pair write (one position at 07 plus both HH
bytes, with paired cache/diagnostic accounting) is a candidate accommodation.
Merely repeating the same failing command is not a proven solution and may hide
evidence. Physical signal corruption needs a signal path fix; a glyph-drive
fault may need display repair. No accommodation was added during that
investigation.

TXHH proves the API accepted the payload and the earlier header, not that the
wire/device did. Its 1B48 prefix is formatted by the logger, with address/payload
copied from the command buffer after payload success; it is not a wire sniffer.
`drop` remains the USB diagnostic queue counter and is NOT a VFD drop count.


## Implemented PD-2200 accommodation: contiguous HH

The current accommodation was added after the historical investigation above.
Physical observations include intended 00 rendered as 08 and 10 rendered as 18.
The exact cause remains unproven. The VFD Output layer now deliberately treats
HH as one field: either dirty HH cell selects `1B 48 07 tens ones`. This applies
to timezone/hour changes, invalidation to --, and reacquisition. Startup already
writes HH contiguously as the beginning of the longer time field at address 07.
Normal HH updates no longer explicitly address 08, avoiding `1B 48 08 30`.
This is a Posiflex accommodation, not a change to UTC, PPS, timezone conversion,
UI, or rolling-indicator semantics. Physical effectiveness still needs bench
verification; the hardware bug is NOT claimed fixed.

The serializer still accepts at most one UART byte per service call. It refreshes
both HH payloads until the tens byte succeeds; that accepted tens freezes the
pair. The ones byte is retried unchanged until accepted, with no intervening
command. A newer desired hour then generates another pair if needed. Cache cells
advance individually only on their corresponding successful payload acceptance,
so a partially submitted pair may temporarily show a mixed cache. If both desired
characters revert to cache before either payload is accepted, the completed
position header may still be left without payload, as before.

TXHH now emits once on pair completion:
`TXHH ms=<ms> pps=<seq> kind=pair bytes=1B4807<tens_hex><ones_hex> want=<HH> cache=<HH> drop=<count>`.
For example `bytes=1B48073030` is a contiguous 00 write. This records API
acceptance, not physical acknowledgment; if desired changed mid-pair, `want` can
differ from the frozen submitted bytes/cache. No per-second logging was added.

The targeted pair test covers requested hour/zone/validity transitions, startup,
every transaction boundary, rejected tens/ones, concurrent label/decade/status
changes and accurate completion events/cache. Existing rendered-screen and
500,000-call fuzz tests now enforce no position-08 command and frozen ones after
accepted tens. Non-HH single-character update behavior is unchanged.
