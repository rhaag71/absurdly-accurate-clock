> Historical diagnostic-build field reference. These temporary fields and the
> observer were removed by the production lifecycle correction. Current NET
> fields and both verified hardware results are in [the investigation](pico-spi-investigation.md).

# Pico SPI diagnostic build

Instrumentation only: no changes to SPI configuration, FIFO pass limits, IRQ
masks, packet construction, reset policy, CS handling policy, or TIME_SYNC.
The existing once-per-minute NET summary retains all previous fields and adds:

- `irq`: all entries to the SPI IRQ handler, including entries while inactive.
- `rxhist`: completed transaction counts in this exact order:
  0, 1–7, 8, 9–15, 16, 17–23, 24, 25–31, 32, 33–39, 40, >40.
  RX is `received` after the existing final CS-rising drain, the same value
  used by `short` and `extra`.
- `irqtxn`: minimum/maximum/last count of active SPI IRQ handler entries for
  completed transactions. Reset the transaction-local count at CS falling.
  An entry is associated by the existing `active` flag, not by sampling CS.
  Repeated low callbacks restart this count even without a completion.
- `rxirq`: cumulative increments of `received` from SPI IRQ drains. Includes
  progress in an active transaction that has not yet completed.
- `rxcs`: cumulative increments of `received` from final CS-rising drains.
- `sent`: minimum/maximum/last `sent` for completed transactions. This counts
  packet bytes written to TX FIFO (including initial preload), not bytes clocked
  on the wire. It caps at 40 and excludes subsequent zero padding writes.

All statistics are since boot, not per-minute deltas; unsigned cumulative
counters wrap modulo 2^32. Before any completion, min/max/last print as zero.
The existing `received` cap is 48, so drain attribution does not count reads
past that cap. Histogram and extrema cover completed transactions only.
The summary snapshots counters with interrupts disabled, then formats in the
main loop. Its buffer is 896 bytes to accommodate all fields at maximum widths.
No ISR prints, added waits, or allocation. The second diagnostic pass adds
read-only hardware observations described below.

## Measurement overhead and ordering

Original hardware operations retain their relative order. Instrumentation adds
work before the SPI handler's existing inactive check and drain, between its
drain and existing cap check/fill, and before the CS-falling latch/preload.
Final-drain accounting and completion statistics run after the existing drain
and before clearing `active` and disabling the peripheral in CR1. Thus these
observations add a small delay before those existing actions; they do not move
or add FIFO service calls. The once-per-minute interrupt-disabled snapshot is
also longer. No timestamps are taken. Bench measurements
are still necessary; the host FIFO model does not model electrical/IRQ timing.


## Raw-state diagnostic pass

The original counters and bucket definitions above are unchanged. New fields:

- `cause=none/ror/rt/rx/tx`: cumulative SPI IRQ entry counts classified by the
  low four MIS bits, sampled before the existing inactive check, drain, or clear.
  Each asserted source increments its own count; multiple sources can count for
  one entry. `none` means MIS was zero when sampled (including stale/pending
  entries), not that the CPU IRQ was never asserted. Includes inactive entries.
- `reselect`: low-level CS callbacks while `active` was already true. Each still
  performs the existing reset. This detects repeated reinitialization within a
  software transaction; it does not identify the physical source of an edge.
- `noirq`: completed transactions with zero active SPI IRQ entries since the
  most recent low-level callback/reset. Repeated low callbacks reset that count.
- `zero=tx/rx/sent/irqs/arm/end`: latest completed zero-active-IRQ transaction.
- `best=tx/rx/sent/irqs/arm/end`: completed transaction with at least one active
  SPI IRQ and the largest final `received` since boot; latest wins ties.
  This is greatest RX progress, not proof of a valid packet at the ESP.
- In both records: `tx` is the cumulative completion ordinal, `rx` is after the
  existing final drain, `sent` is packet bytes queued, and `irqs` is active IRQ
  entries since the latest low callback. All four are decimal. A record remains
  all zero until eligible, then persists across minute reports.
- `arm` and `end` are eight-digit hexadecimal packed register observations:

  | Bits | Value |
  | --- | --- |
  | 3:0 | RIS, offset 0x18 |
  | 7:4 | MIS, offset 0x1c |
  | 12:8 | SR, offset 0x0c |
  | 19:16 | IMSC, offset 0x14 |
  | 23:20 | CR1, offset 0x04 |
  | 24 | SPI1 IRQ enabled in the current core NVIC |
  | 25 | SPI1 IRQ pending in the current core NVIC |
  | 26 | GP9 level sampled through SIO (`gpio_get`) |
  | Other bits | Zero |

  Interrupt bits: bit 0 ROR, 1 RT, 2 RX, 3 TX.
  SR bits: bit 0 TFE, 1 TNF, 2 RNE, 3 RFF, 4 BSY.
  CR1 bits: bit 0 LBM, 1 SSE, 2 MS, 3 SOD.
  `arm` is immediately after SSE and IMSC enable at the end of the low callback;
  `end` is in the high callback BEFORE IMSC=0 and BEFORE the final drain.
  The reads happen in table order, then NVIC enable, pending, and SIO. They are
  sequential, not simultaneous. NVIC pending is a point observation, not an IRQ
  latency measurement. The second NVIC word is used for RP2350 SPI1 IRQ 32.
- `cfg=CR0/CPSR`: hexadecimal saved configuration originally read during begin
  and restored by every reset. This is not a fresh hardware read each minute.

Additional measurement overhead: one MIS read and four bounded source checks per
SPI IRQ; eight read-only hardware observations after arming and before masking
on CS high; a repeated-low check and bounded sample bookkeeping. This extends
GPIO ISR residence after enabling SPI and delays the existing high-side mask,
drain, classification and disable. Original writes and FIFO operations retain
relative order. No DR reads, ICR writes, extra servicing, priority changes, or
CS event-handling changes are introduced. No observation is taken *inside* the
reset-to-slave configuration interval, where extra cycles could extend possible
pin-direction transients. Main-loop counter copying with IRQs disabled grows.

At normal steady configuration expect MS=1, SSE=1, IMSC=0xf and NVIC enable=1
in both snapshots (except the existing RX>=48 cutoff can mask IMSC at the end).
Full TX/empty RX at both endpoints with no pending interrupt points to no FIFO
progress. TXRIS/MIS pending, depleted TX, full RX and/or ROR at the zero-IRQ end
instead point toward CPU IRQ delivery/latency. A snapshot cannot rule out a
condition that asserted and cleared earlier. `reselect` is needed because an
extra low callback resets FIFOs, counters, and the retained arm observation.

## GP9 event/reset diagnostic pass

This pass addresses the measured ~700 extra low callbacks per completion. It
retains every earlier field and uses the same 896-byte once-per-minute NET
buffer. A conservative format-width bound is 849 bytes including NUL (and even
counting escaped CR/LF as four characters), below that capacity.

A link-only diagnostic wrapper on `gpio_set_irq_enabled_with_callback` saves the
original callback per core and registers a forwarding observer. The SDK still
reads/acknowledges events and calls once per pin as before. The observer receives
that exact saved INTS event nibble AFTER the SDK's acknowledgement, records
context, then calls the original Arduino dispatcher exactly once with unchanged
pin/events. It forwards all pins, including PPS. No GPIO/SPI mask, priority,
acknowledgement, mux, pad, or CS decision is changed; no framework file is edited.
`platformio.ini` and the host-test link command both enable this wrapper.

New fields, all cumulative uint32 counters except noted register values:

| Field | Exact order / meaning |
| --- | --- |
| `gcall=low/high/bad` | All csInterrupt invocations classified by the existing gpio_get branch; bad is the subset entered without a current SDK dispatch context for GP9 (includes another GPIO or no context). Total calls = low+high. |
| `gevt=LL/LH/F/R` | Per-bit counts from the saved SDK event argument for correctly associated GP9 CS callbacks: LEVEL_LOW=1, LEVEL_HIGH=2, EDGE_FALL=4, EDGE_RISE=8. Counts overlap for combined events; F+R is not callback count. |
| `gmis=rise_low/fall_high` | Exact event mask 8 but the existing branch read low; exact mask 4 but branch read high. Combined edges and level events are deliberately not classified as mismatches. |
| `gpend=post_ack/reset_window` | First: associated callbacks whose GP9 raw INTR had either edge bit already set at observer entry, after SDK acknowledgement. Second: low callbacks with no raw edge pending immediately before reset but an edge pending just after reset/unreset/CR0/CPSR/CR1=4. |
| `gcfg=enable_force/ctrl` | First is hexadecimal cumulative OR of observed GP9 INTE nibble in bits 3:0 and INTF nibble in bits 7:4, read at GP9 observer entry. Second is fresh full GP9 CTRL, hexadecimal, read in the minute snapshot. Expected `0C/00000001`: edges enabled, no force, SPI function, normal overrides. |

`gcall low` is also the number of transaction-path reset executions, since each
low branch unconditionally resets exactly once (excludes startup spi_init).
`reselect` is its subset with active=true; no extra reset counter or distribution
is necessary to establish whether the callback storm executes resets.

For GP9, raw INTR1 and core-local INTE1/INTF1 use bits 7:4. Reading these registers
has no acknowledgement side effect. `gpend post_ack` is not itself evidence that
acknowledgement failed: an edge may reassert between the SDK write and observer
read. `reset_window` detects new pending edges across a narrow interval, not their
electrical origin. It excludes edges first seen during later latch/fill/SSE work,
or ones already pending before reset. If the interval begins with an edge
already latched, the observation intentionally does not infer another edge.

Measurement ordering: the wrapper adds a forwarding call, saved context, and
three reads for GP9 (INTR, INTE, INTF) before Arduino's dispatcher. Branch counters
run AFTER the original gpio_get decision, so that read is not duplicated for
classification. They precede the original branch work. Two INTR reads bracket
reset/reconfiguration; neither is inside the reset-to-slave MS=0 interval.
Additional bounded counter work runs before packet latch/preload. A CTRL read
and larger stats copy extend the minute snapshot's IRQ-disabled interval.
These observations can alter the rate/timing of a feedback loop; do not require
exactly 700 repeats in the next build. No intentional delays or ISR printing.

Host checks cover exact reset/callback counts for 700 combined-edge repeats,
level/edge/mismatch accounting, a new edge injected during reset versus one
already present after acknowledgement, and forwarding another GPIO without
calling CS. They do not model physical transitions or prove a silicon mechanism.
