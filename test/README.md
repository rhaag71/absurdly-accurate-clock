# Host tests

Run from the repository root:

```sh
sh test/host/run.sh
```

Uses a host C++11 compiler with warnings treated as errors. No PlatformIO/Arduino
installation is required. Binaries live in temporary storage.

- `watchdog_test.cpp` checks normal/watchdog heartbeat intervals, exact toggle
  thresholds, repeated session cadence and unsigned millis wrap using production
  policy helpers. It also checks every-fifth-message reset reporting over 10,000
  messages, normal-boot suppression and fresh per-boot counters.
  Reset-cause hardware, watchdog expiry and actual acquisition
  after a reset require the physical bench procedure in the project README.

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

- `hh_test.cpp` interprets the runtime byte stream into both 20-cell rows,
  including payload-free position commands, instead of assuming four-byte groups.
  It checks all 576 hour pairs at all four command boundaries for both HH cells,
  the decade cell, and all four status/count cells (16,128 cases), with bounded
  FIFO draining, writable=false stalls, and write() rejection. Another 10,000
  seeded repeated transitions cover 08/09/10/18/19/20/23/00/01 without cache resets.
  All 24 hour edges also run through actual PPS association and calendar conversion
  to the rendered screen. Full-frame/cache equality is checked after settling;
  intermediate multi-character updates are intentionally not atomic.
- Hour-range tests reject RMC hours 24..99, check all authoritative hour edges,
  and round-trip every hour in 2000..2099. A negative-epoch helper limitation is
  documented by test; it is not reachable from parser-accepted dates.
- A separately labelled fault-injection test drops an accepted leading-zero
  payload and demonstrates persistent visible `28` with intended/cached `08`.
  This documents missing physical-display feedback, not proof of hardware loss
  or a reproduction of the reported bug under normal transport assumptions.

- `timezone_test.cpp` covers UTC identity; every standard/daylight offset and label;
  all zones one second before/at/after fixed 2024–2027 U.S. DST transitions;
  previous-day/year/leap-day conversion; button bounce/hold/release/cycle/startup
  and millis wrap; complete rendered zone changes and DST transitions under
  positioned writes; unchanged authoritative UTC, rolling phases and lower row;
  acceptance reporting under stalls/rejections/cancellation, and identical byte
  streams with diagnostics observation enabled or disabled.

- `hh_zero_test.cpp` targets physical x0->x8 reports: exact command bytes for
  00/01/10/11/20/21, 1,008 partial-command rendered loss/reacquisition cases,
  300 observed-zone scenarios and 500,000 seeded randomized service calls.
  An independent accepted-byte decoder must match the cache after every call;
  a delayed FIFO-fed screen must converge over both rows. Separate fault-injection
  controls distinguish a lost zero (leaves '-') from D3-high (0->8 and 1->9).
  See `docs/hh-zero-investigation.md`; injections do not prove hardware causation.

- `hh_pair_test.cpp` verifies contiguous `ESC H 07 tens ones` for 01->00,
  11->10, 23->00, invalidation/reacquisition, timezone changes and initial fields.
  Every transaction boundary is tested with competing label/indicator/status
  changes, UART refusal, frozen payload retries, and per-byte cache/completion
  events. Existing HH zero/fuzz tests enforce no explicit position 08 and correct
  five-byte HH sequences. Fault injections remain demonstrations of downstream
  faults, not proof that the physical symptom has been repaired.

- `network_test.cpp` verifies v1's exact 40-byte layout, independent CRC golden
  vectors, corruption/header rejection, signed 64-bit epochs, quality/satellite
  flags, inactive holdover, publication sequence/coherence, invalid/sync gating,
  and observation of actual Timebase acquisition/second edges without mutation.
- `network_transport_test.cpp` compiles the production native SPI1 transport with
  a register/FIFO model to check immutable CS snapshots at every byte split,
  short/extra/stalled transactions, subsequent realignment, TIME_SYNC qualification,
  ignored MOSI, and summary counters. It asserts that CS-boundary FIFO priming
  happens while SSE is clear and that SSE enables only after all initial entries
  are written. Lifecycle regression coverage includes 700
  duplicate low notifications without FIFO destruction, partial-byte abort cleanup,
  explicit/coalesced GPIO events, PPS dispatch coexistence, input-only reset
  overrides, and a separate boot-with-CS-low process. Hardware electrical timing
  and interrupt latency are not modeled.
- Portable ESP32-side reference vectors are in `host/network-v1-vectors.json`.
