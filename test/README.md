# Host tests

Run from the repository root:

```sh
sh test/host/run.sh
```

Uses a host C++11 compiler with warnings treated as errors. No PlatformIO/Arduino
installation is required. Binaries live in temporary storage.

- `timebase_test.cpp` exercises the production GPRMC parser, UTC/PPS association,
  acquisition/correction, pulse loss/cadence, GPS loss, recovery, timestamp and
  sequence wrap, and second/minute/hour/day/month/year/leap-day rollovers.
- Display tests verify 20-cell layout/space placeholders, separate GPS/PPS flags,
  500 ms hold-0, every subsequent 50 ms rolling-decade threshold, final hold-9,
  PPS reset, missed phases,
  unavailable synchronization, timer wrap, and independence from UTC/RMC arrival.
- `vfd_test.cpp` exercises the actual driver and `clock_display::Output` through
  a small Arduino Print stub. It checks unchanged initialization/brightness,
  one-time clear plus short fields, no normal padding bytes, single-character
  updates, all ten-second rollovers, no per-second clears/full-row redraws,
  minimal status changes, and coalescing under UART backpressure/partial headers.
  A simulated screen maps transmitted spaces to zeroes to exercise the workaround.
- The full-row utility still has termination/padding tests, but it is not used by
  the normal clock display. Bounded short-field and position APIs are also tested.

Physical glyph mapping, cleared-cell appearance, UART latency and visual quality
require bench checks. Host tests do not establish the cause of the unit's
space-to-zero behavior.
