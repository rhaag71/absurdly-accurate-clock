# Display timezone and HH diagnostics

GP6 (physical pin 9) is an active-low button to GND using the RP2350 internal
pull-up. No external pull-up is needed. Every boot selects UTC. Stable short
presses cycle UTC → Eastern → Central → Mountain → Pacific → UTC. Selection is
RAM-only. Both press and release must be stable for 30 ms; holding never repeats.
A button held at boot must first be released for 30 ms. Pulses shorter than the
debounce interval are intentionally ignored. Polling uses wrap-safe millisecond
subtraction and no delays or button interrupts.

`presentation::convertUtcForDisplay` produces a separate civil calendar value,
abbreviation, UTC offset and daylight flag. Only the renderer's time/zone fields
use it. The authoritative UTC epoch, calendar, PPS association, GPS logic and
rolling-decade phase remain unchanged. There are no writes back to the timebase.
The existing top-row label cells 3–5 and time cells 7–14 (zero-based) are retained;
all changes use the existing changed-character/positioned-write path.

Rules are contemporary U.S. DST rules (2007 onward), **not historical timezone
rules**. Mountain observes DST; this is not an Arizona mode. Standard offsets are
−5/−6/−7/−8 hours, with +1 hour in daylight time. For each UTC calendar year,
calculate the second Sunday in March and first Sunday in November. Start is local
02:00 converted using the standard offset; end is local 02:00 converted using the
daylight offset. Compare the UTC epoch against the half-open [start, end) interval.
Then offset a separate epoch and convert its complete calendar, including day,
year and leap-day rollovers. No ambiguous local-time lookup or timezone database.

Labels are UTC, EST/EDT, CST/CDT, MST/MDT, PST/PDT. Without valid UTC/date,
non-UTC modes show E--/C--/M--/P-- with the existing unavailable time placeholders;
this avoids falsely asserting a standard/daylight state before synchronization.

## USB diagnostics

Existing GPS/PPS transition messages remain. New ASCII records end in CRLF:

```text
ZONE ms=<uptime_ms> zone=<UTC|Eastern|Central|Mountain|Pacific> drop=<count>
HH ms=<uptime_ms> pps=<sequence> epoch=<UTC_unix_seconds> valid=<0|1> utc=<YYYY-MM-DDTHH:MM:SS> zone=<name> mode=<UTC|STD|DST|?> off=<signed_hours> civil=<YYYY-MM-DDTHH:MM:SS> want=<HH> cache=<HH> drop=<count>
TXHH ms=<uptime_ms> pps=<sequence> kind=pair bytes=1B4807<tens_hex><ones_hex> want=<HH> cache=<HH> drop=<count>
```

`ZONE` records an accepted button press. `HH` records the first frame and every
change in desired HH or zone label (also every zone selection). It includes the
cache **before** that loop's output service. Valid=0 makes the calendar/offset
fields non-authoritative; mode=? and want=-- explicitly mark unavailable time.
It does not log every second or rolling-indicator update.

`TXHH` records completion of one contiguous HH pair accepted by UART, after
updating both cache cells. For example, `bytes=1B48073030` means ESC H, address
07, ASCII `00`. HH always starts at 07; normal updates never position directly
at 08. The header/tens can have been accepted on earlier loop iterations. Both
payloads refresh until tens is accepted, then freeze until ones succeeds. A
newer desired hour is submitted as another pair afterward. Thus `want` may
differ from the recorded frozen pair. The cache updates per accepted byte;
partial cache values in HH records are expected. A canceled header-only command
produces no TXHH record. These are firmware/UART observations, **not physical
VFD readback**. See [HH accommodation](hh-zero-investigation.md#implemented-pd-2200-accommodation-contiguous-hh).

Formatting and queueing occur in the main loop only on these events. The bounded
USB queue is 1024 bytes and drains at most 64 bytes per loop, only when writable.
Disconnected/full USB never causes a wait: whole records are dropped. `drop` is a
cumulative counter of dropped diagnostic messages, included in subsequent new
records; do not treat a capture with drops as a complete transmission history.
No flash logging or periodic refresh is added. The paired-HH accommodation
retains nonblocking retries; its effectiveness requires physical verification.

For the overnight run, capture USB serial at 115200 on the ThinkPad before the
interesting hour transitions, and keep the connection open. Photograph any bad
HH with a timestamp, retaining surrounding HH/TXHH/ZONE lines and drop counts.
Correct desired/cached/accepted bytes still cannot exclude corruption downstream
of the UART API; wire capture at the VFD input remains the next discriminator.

## Bench checks after a separate upload

- Boot/reboot in UTC, including boot while holding the button.
- Press once per zone; hold several seconds; release and press again; verify wrap.
- Confirm current standard/daylight abbreviations and previous-local-day hours.
- Press during the rolling animation and near PPS: phase and status remain stable.
- Check HH/TXHH on hour and zone changes; ordinary seconds produce no new records.
- Disconnect/reconnect USB while running; clock/button must remain responsive.
- Preserve any impossible HH evidence rather than assuming this feature fixed it.
