# Host tests

Run from the repository root:

```sh
sh test/host/run.sh
```

Uses a host C++11 compiler, no PlatformIO/Arduino installation required. Tests
exercise the production parser, timebase, display formatting/diff logic, and
actual VFD encoder with a small Arduino Print stub. Binaries live in temporary
storage. Includes every seconds digit transition, minute/hour/day/month/year
and leap-day rollover, two-label acquisition, boundary-only corrections,
repeated labels, late/edge-straddling/fractional messages, checksum rejection,
PPS loss, bad cadence, missed edges, GPS loss, recovery, UART-stall invalidation,
32-bit timestamp/sequence wrap, and direct-position UART bytes.

Physical phase, polarity, receiver buffering, and VFD latency need bench checks;
these tests cannot establish the receiver's absolute PPS/RMC relationship.
