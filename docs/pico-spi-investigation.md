# Pico SPI1 investigation: hardware sample and installed implementation

Current status: both the reset-storm lifecycle correction and separate TX
first-byte alignment correction are hardware-verified. See [reset-storm evidence](#production-correction--reset-storm-hardware-verified-2026-09-27)
and [final hardware acceptance](#final-hardware-acceptance--both-spi-failures-resolved).
Earlier sections below are the chronological diagnostic-only investigation,
not statements of current fix status. See [historical field definitions](pico-spi-diagnostics.md).

## Earlier interpretation (superseded by the reselect measurements below)

The leading failure class is failure to establish/maintain a shifting, armed
SPI slave during the external transfer, with reset/CS restart timing the first
software mechanism to investigate. A simple missing FIFO interrupt is not an
adequate explanation of all the observations. This is a hypothesis, not a
confirmed root cause or evidence that any particular reset workaround works.

Given the reported 588 completions, the histogram says exactly 539 finished at
RX=0, none at 40. IRQ min/max/last alone does not give the number of transactions
with zero IRQs, and TX min/max/last does not establish a TX distribution. The new
`noirq` counter supplies the former. The occasional valid ESP packet has not
been correlated with an individual Pico completion in this sample.

If 40 frames actually enter an enabled receiver with no reads and no intervening
reset, an eight-entry FIFO cannot end empty without overrun. Likewise, an armed
transmitter containing the ACT1 header should shift its preload even without
CPU servicing. Entirely zero responses, RX=0, final drain=0, and overrun=0 point
upstream of servicing alone, or to repeated resets erasing evidence. These are
inferences assuming the physical clocks/selection reach the peripheral and the
existing counters faithfully observe those hardware transactions.

Rare servicing and valid packets show that the path can work under some timing
conditions. A transaction that stays armed can use exactly the existing IRQ
handler to refill all 40 packet bytes. A restart late in a physical transfer
could erase RX accounting after useful TX data has already left. Neither scenario
has yet been demonstrated on this bench. CRC success establishes that particular
packet's integrity; it does not establish stable selection/timing on every poll.

## A. What the application code proves

`src/network_interface.cpp`, `csInterrupt`, `spiInterrupt`, `fill`, and `drain`:

1. A CS callback chooses its branch by the current SIO GP9 input level, not the
   latched edge that caused the interrupt.
2. Every low branch masks SPI sources, asserts SPI1 reset, releases reset and
   waits for RESET_DONE, restores saved CR0/CPSR, then writes CR1=4 (slave,
   disabled). The SPI pin mux remains connected throughout.
3. It latches the 40-byte packet, zeros sent/received/overrun and active-IRQ
   accounting, marks active, and fills at most eight FIFO slots. Then CR1=6
   enables the slave and IMSC=0xf enables all four SPI sources.
4. The SPI handler masks sources and returns if inactive. Otherwise it drains
   at most eight entries, clears RT/ROR via ICR, and fills at most eight slots.
   At received>=48 it masks sources and returns without filling. `sent` caps
   at 40; later writes are zero padding. `received` caps at 48.
5. The high branch masks SPI sources, makes one final drain if active, counts
   completion/short/extra/overrun, clears active, and writes CR1=4. It does not
   reset the peripheral or drain TX on this branch.
6. There is no active-state guard on a low callback. An extra low callback
   resets both FIFOs and all transaction-local accounting again. Matching
   completion rates therefore does NOT establish one reset per physical poll.
7. The ISR and final drain are the only RX consumers; the project uses no SPI
   DMA, SPI Arduino library, second application core, or later SPI handler
   installation. Main-loop critical sections cover small state copies and the
   existing qualified TIME_SYNC emission. No deliberate 3.2 ms IRQ exclusion
   was found. Their real worst-case latency has not been measured.

The new diagnostic reads do not change this sequence of hardware writes/FIFO
operations, but add execution time at the locations documented separately.

## B. Hardware documentation

The installed RP2350 generated `hardware/regs/spi.h` and `hardware/structs/spi.h`
are the local register evidence. SPI1 is at 0x40088000. Relevant offsets:

| Register | Offset | Relevant meaning |
| --- | --- | --- |
| CR0 | 0x00 | DSS=7: eight bits; FRF=0: Motorola; SPO=0/SPH=1: mode 1 |
| CR1 | 0x04 | MS bit 2 resets to zero (master); SSE bit 1 resets to zero |
| DR | 0x08 | RX read / TX write |
| SR | 0x0c | TFE=1, TNF=2, RNE=4, RFF=8, BSY=16 |
| CPSR | 0x10 | Even prescaler, 2..254 |
| IMSC | 0x14 | Enable bits ROR=1, RT=2, RX=4, TX=8; reset=0 |
| RIS | 0x18 | Raw requests before masking; reset=8 |
| MIS | 0x1c | Requests after masking; reset=0 |
| ICR | 0x20 | Write bit 0 to clear ROR, bit 1 to clear RT; no TX/RX clear bits |

[ARM PL022 TRM, sections 2.3.3 and 3.4](https://documentation-service.arm.com/static/5e8e3bc7fd977155116a9361):
TX requests assert at four or fewer FIFO entries, RX at four or more. TX is not
qualified by SSE. Priming eight words while disabled is explicitly supported.
RT requires a nonempty RX FIFO and a fixed 32-bit idle period; emptying RX,
receiving new data, or writing RTIC deasserts it. ROR denotes receiving another
frame with a full FIFO; existing FIFO contents survive. The combined interrupt
is the OR of enabled requests. The generated RXIM description's “half full or
less” wording is misleading; section 3.4.1 explicitly specifies four or more.
Do not assume the slave timeout is exactly 320 us from the ESP clock without
measuring its actual behavior.

[RP2350 datasheet, sections 12.3.1–12.3.4](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf):
TX and RX FIFOs each hold eight entries. Slave transfers use external SCK;
clk_peri is the internal SSP reference and must be at least 12 times external
SCK. Mode 1 permits continuous words under one selection. Reset disables the
logic; configure before SSE. SCK/CS are outputs in master mode and inputs in
slave mode. RP2350 also connects peripheral-controlled TX output enable for
slave deselection.

There is no identified specification that requires a new TXIM edge after
unmasking, or that permits loss of a persistent FIFO-level request simply
because the FIFO was primed before SSE. Once external clocks consume TX, its
request should assert again. Reset necessarily removes prior FIFO/state
history; it does not itself reset the NVIC vector or enable. Nothing inspected
establishes that reset while selected reliably arms before the first ESP clock.

## C. Installed Arduino-Pico / Pico SDK evidence

Inspected installation root:
`/home/rob/.platformio/packages/framework-arduinopico`
(build reports `1.60100.0+sha.fd65f6d4`), platform
`5d4561a05e3b212660ac6fdd3fbfb328d1988aa1`, ARM Pico 2.
Paths below are relative to that root; line numbers refer to this installation.

| Source | Evidence |
| --- | --- |
| `cores/rp2040/Arduino.h:87` | digitalPinToInterrupt is identity |
| `cores/rp2040/wiring_private.cpp:103` | dispatcher ignores events, directly invokes pin callback |
| `cores/rp2040/wiring_private.cpp:126` | CHANGE enables falling\|rising; registration uses noInterrupts/interrupts; no mux/direction change |
| `pico-sdk/src/rp2_common/hardware_gpio/gpio.c:153` | default ISR reads per-core GPIO events, acknowledges before callback, walks GPIO order |
| `pico-sdk/src/rp2_common/hardware_gpio/gpio.c:186` | GPIO masks/callback belong to registering core; enabling callback enables IO_IRQ_BANK0 |
| `pico-sdk/src/rp2_common/hardware_gpio/gpio.c:35` | gpio_set_function sets pad input enable, clears overrides, removes isolation |
| `pico-sdk/src/rp2_common/hardware_gpio/include/hardware/gpio.h:894` | gpio_get reads SIO input without changing function |
| `pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/intctrl.h:37` | IO_IRQ_BANK0=21; SPI1_IRQ=32 at line 48 |
| `pico-sdk/src/rp2_common/hardware_irq/irq.c:60` | irq_set_enabled selects num/32 bank and num%32 bit; enabling clears pending then sets enable |
| `pico-sdk/src/rp2_common/hardware_irq/irq.c:254` | exclusive-handler installation asserts old handler is unhandled or same, then updates vector under SDK lock |
| `pico-sdk/src/rp2_common/hardware_irq/irq.c:691` | runtime initializes priorities to default 0x80 |
| `pico-sdk/src/rp2_common/hardware_resets/include/hardware/resets.h:111` | reset/unreset operate on reset-controller bits; wait polls RESET_DONE |
| `pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/resets.h:84` | SPI1 reset is bit 19, 0x00080000 |
| `pico-sdk/src/rp2_common/hardware_spi/spi.c:21` | spi_init resets block, sets baudrate/format, enables DREQ then SSE; runtime resets clear DREQ again, unused here |
| `cores/rp2040/main.cpp:172` | second core launches only with setup1/loop1; setup runs on main core |
| `libraries/SPISlave/src/SPISlave.cpp:236` | this library could install SPI1 handler only if its begin is invoked; it is not used/linked here |

The existing linked ELF was disassembled as an additional implementation check:
`irq_set_enabled(32,true)` addresses NVIC bank 1, not an overflowing 32-bit
mask in bank 0. Runtime priority initialization writes 0x80808080 across all
52 external IRQ priorities. The CS ISR uses SPI1 base 0x40088000 and reset bit
0x80000. The compiled sequence agrees with the source.

Both GPIO and SPI registration happen in setup on core 0. No application change
to their default equal 0x80 priorities was found. Thus SPI cannot preempt the
GPIO handler. A same-priority pending GPIO IRQ (lower exception number) can win
arbitration ahead of SPI; it does not preempt an already running SPI handler.
A storm of GPIO callbacks could therefore defer SPI. PPS shares the GPIO
handler but its callback is short. GPIO registration masks interrupts only
during setup; it does not hold the global mask over ordinary callbacks.

USB/timer/UART ISRs and brief interrupt-disabled sections can defer execution.
They do not provide evidence of replacing SPI1's vector or permanently masking
it. Handler replacement by an unused library is not an explanation supported by
this build. There is no GP9 SPI-function exclusion in the GPIO IRQ implementation.
There IS a relevant interaction to measure: both the SPI peripheral and GPIO
edge detector observe this pad while software resets the peripheral.

## D. Remaining hypotheses and next bench test

Most specific software candidate: reset/reconfiguration while CS is active,
possibly repeated because callbacks sample current level rather than retain edge
identity. Reset restores master mode until CR1=4 is rewritten while GP9/GP10
remain SPI-muxed. A direction/output transient could cause a GPIO event or bus
contention; a delayed event observed with CS low could then reset again. This
is a plausible feedback path, NOT a measured glitch or documented silicon erratum.
No probe is added inside that interval to avoid extending it. Other candidates
are arming after clocks start, input/selection integrity, or an actual pending
SPI IRQ being deferred. The current measurements do not distinguish them.

Earlier proposed experiment (superseded: do not use a scope yet): run this diagnostic UF2 with the same ESP
firmware/poll settings, collect at least two minute summaries, and concurrently
capture GP9 CS, GP10 SCK and GP11 MISO at the Pico pads with a scope/logic analyzer.
Include failed and valid frames; use enough bandwidth/analog visibility to see
short CS transients, not just a 100 kHz decoded bus. Do not modify priorities,
add setup delay, or change ESP behavior during this comparison.

Interpret the retained `zero` versus `best` records:

- `reselect` increasing demonstrates extra resets while software still active;
  it does not alone prove reset-generated edges. Inspect captured CS activity.
- Zero-IRQ end with TX full, RX empty, no requests, CR1=6 and IMSC=f means no
  retained FIFO progress, not a missing pending TX interrupt. Look at reselect
  and external selection/clocks. Arm CS=1 means software finished arming after
  the selection had already ended (or sampled a transient).
- Zero-IRQ end with pending MIS/TXRIS, TX depleted and RX full/ROR instead
  supports IRQ delivery/latency. NVIC enabled/pending helps separate peripheral
  masking from delivery; a single pending sample cannot quantify latency.
- `cause` determines whether the rare handler entries were TX/RX threshold,
  timeout, overrun, or no-longer-pending requests. Combined sources overlap.
- `best` establishes hardware state for greatest RX progress, but cannot by
  itself be paired to the ESP valid-packet log. It records the Pico completion
  ordinal for correlation. Intermediate events can escape endpoint snapshots.

## Previous-pass validation and conditional fix direction (not implemented)

All existing host tests pass, including new injected-register bookkeeping checks
for pre-mask MIS capture, paired records, repeated-low reset observation and
multi-source counting. These checks do not simulate electrical effects, timeout,
interrupt arbitration, or reset timing. `git diff --check` passes.
`pio run -e pico2 -j1` succeeds: RAM 11660 bytes, flash 50248 bytes. PlatformIO
reports its existing multiple-Core-version warning. No flash/commit/push.

Only if the reset/arming hypothesis is confirmed: move the required reset and
preload preparation to the deselected interval so SPI is already configured as
a slave before CS falls, preserving packet-latch and abort/recovery semantics.
That is the smallest likely *direction* of a fix, not yet a justified patch;
packet freshness must be reviewed before choosing its exact implementation.
Merely raising IRQ priority or ignoring extra low callbacks would not address
an electrical transient or late arming. If the new capture instead demonstrates
pending-but-unhandled IRQs, investigate that measured delivery failure first.


## Reselect investigation: unchanged ESP bench measurements

These two consecutive summaries are the evidence for this pass. The ESP stayed
at mode 1, 100 kHz, 40 bytes, approximately 10 transactions/second throughout.

```text
NET seq=67 epoch=1790548132 flags=6F tx=588 short=588 extra=0 over=0 sync=58 skipped=0 irq=771
rxhist=553,5,0,7,0,5,0,9,1,8,0,0 irqtxn=0/39/0 rxirq=770 rxcs=0 sent=8/40/8 cause=1/0/770/0/0
reselect=407198 noirq=553 zero=588/0/8/0/056F1000/056F1000 best=235/39/40/39/016F1200/056F1200 cfg=F987/06

NET seq=130 epoch=1790548192 flags=6F tx=1176 short=1176 extra=0 over=0 sync=118 skipped=0 irq=1268
rxhist=1120,10,0,7,0,9,0,15,1,14,0,0 irqtxn=0/39/0 rxirq=1267 rxcs=0 sent=8/40/8 cause=1/0/1267/0/0
reselect=819273 noirq=1120 zero=1176/0/8/0/016F1200/056F1200 best=928/39/40/39/016F1200/056F1200 cfg=F987/06
```

Delta: 588 completions, 412075 reselections (700.8078 per completion), 567
zero-IRQ completions, 497 SPI IRQ entries, 497 accounted RX bytes, all those
new SPI entries reporting RT in MIS. No final-drain bytes or overruns.

Decoded endpoint words: all have RIS=MIS=0, IMSC=f, CR1=6, NVIC enable=1,
pending=0. SR is 0x10 or 0x12: RX empty; TX not empty; 0x10 has TX full, 0x12
has room. BSY alone does NOT prove external shifting (nonempty TX is sufficient
according to the SR definition). 0x056F1000 has CS=1, including the first zero
record's *arm* observation after a callback which initially read CS low.
0x016F1200 has CS=0; 0x056F1200 has CS=1. That first record proves the two software
level observations differed, not why the signal changed or how many times.
CR0=F987 decodes as SCR=249, eight-bit Motorola mode 1; CPSR=6. No change made.

### Current conclusion: proven repeated resets, inferred feedback source

The reselect counter proves hundreds of extra low-classified callbacks while
software remained active. Each executes another hard SPI1 reset. Thus the old
per-transaction IRQ and RX values describe only the segment after the *latest*
low callback, not necessarily the whole physical ESP selection. Reset erases
FIFO contents, received, overrun, sent and transaction_irqs. This explains why
noirq/RX=0/final-drain=0/overrun=0 can coexist with clocks elsewhere in the same
physical transaction. Matching completion rates does not contradict this.

Most likely: a self-retriggering GPIO/reset cycle. GP9 is still SPI-muxed when
SPI reset returns MS to master; restoring slave mode changes the peripheral's
control of CS/SCK again. If this produces a new CS input transition, the GPIO
edge detector latches it during the callback, and the next callback can again
read low and reset. Rare gaps in that cycle allow RT servicing and enough TX
progress for an intact response. This reconciles both classes of observations
but the source of the fresh GPIO requests is NOT yet proven. Genuine external
noise, forced/level requests, or unexpectedly uncleared latches must be measured
before labeling an electrical feedback mechanism as confirmed.

### Counter audit (application source and host bookkeeping)

`reselect` has one increment site in `csInterrupt`, after the high branch has
returned, guarded by `active`. There is no increment loop, macro expansion, or
second writer. One invocation reads low, increments at most once, then reaches
one reset call. It is copied with interrupts disabled and printed as uint32 via
unsigned long. Counts here are far below wrap. SPI IRQs and the main loop cannot
increment it. One application registration binds csInterrupt to GP9; PPS uses a
different callback on GP2. A host test now injects 700 repeated combined-edge
callbacks and checks exact increments and actual reset-call counts.
This rules out a bookkeeping multiplication in the inspected program; it is
not a blanket proof against arbitrary RAM corruption on the board.

### Exact GPIO dispatch and register evidence (installed sources)

Paths below retain the installation root specified in section C.

1. `cores/rp2040/wiring_private.cpp:126`: CHANGE maps to `4 | 8`, not level
   low/high (`1`/`2`). Arduino stores a callback in `_gpioIrqCB[pin]`, sets the
   pin's software enable bit, and calls gpio_set_irq_enabled_with_callback.
   There is no SPI-function check or mux write in attachInterrupt.
2. `pico-sdk/src/rp2_common/hardware_gpio/gpio.c:173`: the enable helper first
   acknowledges stale requested events, then ORs them into that pin's INTE
   nibble. On first Arduino registration it does not independently clear any
   hypothetical previously enabled level bits; none are enabled elsewhere in
   this application's inspected startup. The new capture checks actual INTE.
3. `gpio.c:153`: one per-core default handler reads a snapshot of INTS for each
   group of eight pins. It extracts each pin's four-bit nibble, acknowledges that
   nibble, and calls `callback(pin, events)` ONCE if nonzero. `events=12` is one
   callback, not two. It moves to the next pin; it does not loop on that pin's
   status. A fresh request causes later IRQ entry/dispatch.
4. `hardware_gpio/include/hardware/gpio.h:582`: acknowledgement writes
   `event_mask << (4*(gpio%8))` to INTR[gpio/8]. GP9 is word 1, bits 7:4.
   The generated `rp2350/hardware_regs/include/hardware/regs/io_bank0.h:9038`
   marks its EDGE_HIGH/LOW bits (0x80/0x40) WC; LEVEL_HIGH/LOW (0x20/0x10) RO.
   An asserted level is not cleared by acknowledging. An INTF forced bit also
   survives INTR acknowledgement. A new edge after the acknowledgement can
   latch immediately, including while the user's callback is still running.
5. `wiring_private.cpp:103`: the Arduino dispatcher ignores events but indexes
   the callback by the pin supplied by the SDK. GP2/PPS does not dispatch GP9
   merely because both share IO_IRQ_BANK0. Discarding edge identity allows a
   rise event to be handled as low if the input has since fallen; it cannot
   by itself regenerate the event or repeat the call.
6. `hardware_gpio/include/hardware/gpio.h:894`: gpio_get reads SIO GPIO_IN.
   It neither acknowledges events nor changes the mux. The RP2350 datasheet
   table 5 / section 9.4 confirms SIO input remains connected independently of
   output FUNCSEL, and section 9.5 describes latched edges versus live levels.
   [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
7. SPI1 reset bit 19 is separate from IO_BANK0 bit 6 and PADS_BANK0 bit 9.
   Our reset write therefore does not directly clear GPIO INTE/INTR, reset GP9's
   mux/pad, or acknowledge its edge. Peripheral output/direction changes can
   indirectly alter the pad input. That latter effect is a hypothesis to test,
   not an observed reset-controller write to the GPIO block.

Relevant IO_BANK0 offsets: GP9 CTRL=0x04c; INTR1=0x234; core-0 INTE1=0x24c,
INTF1=0x264, INTS1=0x27c. All event nibbles use LL/LH/fall/rise bits 0/1/2/3.
IRQOVER and INOVER are independently configurable in CTRL. The application
leaves both normal; diagnostics report CTRL rather than assuming that state.

An unrelated implementation detail: this installed Arduino no-parameter path
uses the parameterized function-pointer branch with an unused argument. The
actual CS function has no parameters, and the linked ARM code ignores that
argument. It does not create a loop or explain repeated dispatch; no change
was made to Arduino or this calling convention.

### Minimum next observation, with original dispatch/acknowledgement retained

The SDK already has the exact pre-acknowledgement INTS nibble in its callback
argument, but Arduino drops it. The diagnostic build uses a linker wrapper on
`gpio_set_irq_enabled_with_callback` to insert a forwarding observer. This
captures the supplied GPIO number/event mask and then invokes the SAME original
Arduino dispatcher ONCE, with unchanged arguments. It does not install a raw
handler, change masks, add acknowledgements, or replace event-to-callback policy.
No installed framework files were modified. PPS callbacks still forward too.

The observer reads GP9 raw INTR just after the SDK acknowledgement, and actual
INTE/INTF. csInterrupt counts its existing sampled high/low branch and compares
that decision with the saved event mask. Two further INTR reads bracket only the
existing reset/unreset/CR0/CPSR/CR1 restoration; they detect clear-before and
edge-pending-after. There are no reads inserted inside the MS=0 interval.
The read after CR1=4 precedes packet latch/fill/SSE; later retriggering is outside
this narrow bracket. Full field definitions are in pico-spi-diagnostics.md.

Interpretation of the next firmware-only run (no scope or ESP change):

| Observation | Distinguishes |
| --- | --- |
| `gcall` low rises with reselect, bad-context=0 | Real GP9 callback entries; no counter multiplication or other-pin dispatch |
| `gevt` level bits nonzero / `gcfg` enable nibble has bits 0/1 | Level-sensitive source; acknowledging cannot silence a held level |
| `gcfg` force nibble nonzero | Software-forced request; INTR writes cannot clear the force |
| Edge counts large, expected enable=C/force=0, post-ack count low | SDK acknowledgement takes effect; fresh edges drive the storm |
| `gpend` reset-window count near low-callback count | Fresh edges repeatedly arise across reset/reconfiguration, strongly supporting feedback |
| `gpend` post-ack count large | Edge already pending immediately after acknowledgement: retained or rapidly reasserted; does not distinguish those two alone |
| `gmis` rising-only/low large | Direct proof that saved edge identity and the callback's current-level classification disagree |
| Reset-window count low | Does not clear reset/SSE of suspicion: edges may arise before/after bracket; investigate event source from the remaining fields |
| `gcall` bad-context nonzero | Unexpected callback path/registration; investigate before using its event correlations |

Fresh GPIO edge latches prove detected input transitions, not their electrical
origin (external CS, crosstalk, or peripheral-induced pad effects). An event
already pending on the first post-ack read may have reasserted between the
write and read. This instrumentation cannot prove those distinctions by itself.
Do not call a retained INTR bit an acknowledgement bug without that qualification.

### Permanent record and fix status

Historical status when this diagnostic build was prepared: a functional fix was
not yet authorized, and the next step was a firmware-only diagnostic run. That
run's measurements and the subsequent authorized fix are recorded in the later
sections. No scope measurement was made.

Historical decision at this stage: additional reset-window GPIO evidence had not
yet been collected, so no fix had been selected. The later investigation below
confirmed a reset-correlated event storm and implemented deselected recovery plus
CS/SCK output-enable suppression. The final hardware acceptance section records
its outcome and preserves the unmeasured analogue-transient caveat.

Historical note: the instruction above was current at the time of the diagnostic
build. It was superseded by the later fix and hardware measurements in this
record; the exact analogue pad transient remains unmeasured.


### This-pass verification

- `sh test/host/run.sh`: all tests pass, including the 700-repeat/reset and
  original-dispatch forwarding checks.
- `git diff --check`: passes.
- `pio run -e pico2 -j1`: succeeds; RAM 11740 bytes, flash 50832 bytes.
- Linked ARM disassembly confirms attachInterrupt calls the diagnostic wrapper,
  which delegates to the real SDK registration. The SDK default handler still
  writes the acknowledgement before its indirect callback call.
- No flash, commit, push, ESP change, scope test, transport fix, IRQ-priority
  change, or wiring change. Existing `pico-spi.log` is untouched.

## Production correction — reset storm; hardware-verified 2026-09-27

### Symptom and decisive measurements

Pico classified essentially every transaction SHORT; ESP normally received zeros /
BAD MAGIC, with occasional complete CRC-valid ACT1 packets. ESP firmware, wiring,
mode 1, 100 kHz, 40-byte requests and polling remained unchanged for these three
consecutive diagnostic summaries:

```text
NET seq=64 epoch=1790549460 flags=6F tx=588 short=588 extra=0 over=0 sync=58 skipped=0 irq=377
rxhist=567,3,1,5,0,7,0,2,1,2,0,0 irqtxn=0/37/0 rxirq=376 rxcs=0 sent=8/40/8 cause=1/0/376/0/0
reselect=329067 noirq=567 zero=588/0/8/0/016F1200/056F1200 best=515/37/40/37/016F1200/056F1200
cfg=F987/06
gcall=329655/629/0
gevt=0/0/330058/329696
gmis=0/0
gpend=7/329486
gcfg=0C/00000001

NET seq=125 epoch=1790549520 flags=6F tx=1176 short=1176 extra=0 over=0 sync=118 skipped=0 irq=743
rxhist=1135,7,3,10,0,9,0,5,1,6,0,0 irqtxn=0/39/0 rxirq=742 rxcs=0 sent=8/40/8 cause=1/0/742/0/0
reselect=658341 noirq=1135 zero=1176/0/8/0/056F1000/056F1000 best=695/39/40/39/016F1200/056F1200
cfg=F987/06
gcall=659517/1252/0
gevt=0/0/660347/659593
gmis=0/0
gpend=12/658919
gcfg=0C/00000001

NET seq=194 epoch=1790549580 flags=6F tx=1764 short=1764 extra=0 over=0 sync=178 skipped=0 irq=1190
rxhist=1701,12,3,16,0,11,0,8,2,11,0,0 irqtxn=0/39/0 rxirq=1189 rxcs=0 sent=8/40/8 cause=1/0/1189/0/0
reselect=986444 noirq=1701 zero=1764/0/8/0/056F1000/056F1000 best=695/39/40/39/016F1200/056F1200
cfg=F987/06
gcall=988208/1868/0
gevt=0/0/989471/988312
gmis=0/0
gpend=19/987732
gcfg=0C/00000001
```

The preceding measurement that first exposed the storm increased `reselect` from
407198 to 819273 while completions increased from 588 to 1176: 412075 extra
SPI1 resets across 588 new completions, roughly 700 per transaction. The three
summaries above then captured the reset-window GPIO evidence in a separate
instrumented run. Do not merge counts from those runs.

### Root cause and strength of evidence

The reset-on-selection strategy creates a self-sustaining CS/reset feedback loop.
A GP9 edge invokes the callback; current CS reads low, so SPI1 is reset while
GP9 remains SPI-muxed. Fresh GP9 edge bits arise across reset/reconfiguration.
The bank IRQ dispatches again, CS again reads low, and the callback destroys and
reloads the FIFO again. This is adequately established to correct the lifecycle;
another diagnostic run is not necessary before that correction.

Evidence is deliberately separated:

- **A — Application/observer source:** `reselect` had exactly one increment per
  callback observing low while already active, followed by exactly one SPI reset.
  Independent low-callback counting had one increment per invocation too. The
  observer forwarded the original callback once. `gpend`'s second count required
  an edge clear immediately before reset and pending after reset/unreset and
  CR0/CPSR/CR1 restoration, before packet latch/preload/SSE. It did not count each
  loop iteration or each edge bit separately. Diagnostic reads added observation
  time around this interval, without changing reset/dispatch policy.
- **B — Hardware:** second-minute deltas are 588 completions, 329274 reselections,
  329862 low callbacks and 329433 reset-window edge occurrences; third-minute
  deltas are 588, 328103, 328691 and 328813. There is a small unresolved counter-consistency anomaly: the third
  reset-window delta exceeds low-callback delta by 122, impossible for a strict
  cumulative subset with atomic snapshots. The archived linked diagnostic
  disassembly copies these fields before restoring PRIMASK (0x10004a84 through
  0x10004b80); sampling skew is not an established explanation. Preserve the
  supplied numbers, do not silently correct them or treat the ratios as exact
  probabilities. Cumulative reset-window counts remain close to low counts:
  329486/329655, 658919/659517, 987732/988208. Resolving this small discrepancy
  would require the exact flashed artifact/log provenance, but it does not
  negate the independently consistent low=reselect+tx identity or the enormous
  population of observed clear-before/pending-after reset windows.
  There are roughly 558–560 reselections per completion in this run (the older
  run was roughly 700). Level events and bad callback context are zero, mask=C,
  force=0, normal GP9 CTRL=1. Only 19 edges were already pending immediately
  after acknowledgement across nearly a million low callbacks. New events
  predominantly appear inside the reset window, rather than failing to clear.
  `gmis=0` excludes single-edge/current-level disagreement; it does **not**
  exclude combined rise+fall notifications ending low. Both edge counters
  approach the low-callback count, consistent with precisely that case.
- **C — Documented hardware:** SPI CR1.MS resets to 0 (master), and SPI-muxed
  CS/SCK direction depends on master/slave mode. GPIO output-enable override
  DISABLE=2 prevents peripheral output drive without disconnecting input sensing.
  SPI1 reset is independent of IO_BANK0/PADS_BANK0, so GPIO events and overrides
  survive it. GPIO edge bits latch transitions and are write-one-to-clear;
  reading SIO input works independently of output mux selection.
- **D — Installed implementation:** Arduino-Pico
  `1.60100.0+sha.fd65f6d4`, `cores/rp2040/wiring_private.cpp:103,126`, maps CHANGE
  to 4|8 and discards callback edge identity. SDK
  `pico-sdk/src/rp2_common/hardware_gpio/gpio.c:153` reads each pending pin nibble,
  acknowledges before callback, calls once per nonzero pin nibble, then advances
  to the next pin. It does not repeatedly call GP9 for the same local snapshot.
  Fresh pending edges cause subsequent bank IRQ handling. The earlier linked
  disassembly audit confirmed this acknowledgement/callback ordering and the
  observer's single forwarding call. No wrong-pin dispatch or priority defect
  is needed to explain the measurements.
- **E — Remaining inference:** the precise pad waveform/drive contention has not
  been measured, so temporary master output drive is the register-supported
  explanation for the regenerated edges, not a captured analogue waveform.
  Firmware instrumentation cannot logically rule out external edges coinciding
  with reset. Their overwhelming reset-window association, unchanged ESP,
  and documented direction hazard make reset feedback the actionable cause.
  Post-fix electrical/timing acceptance is still required.

Register/source references: installed RP2350 `hardware/regs/spi.h`, CR1.MS reset;
`hardware/regs/io_bank0.h`, GP9 CTRL.OEOVER and INTR1/PROC0_INTE1/INTF1/INTS1;
`hardware/regs/resets.h`, SPI1 versus IO_BANK0/PADS_BANK0 reset bits;
SDK `hardware_gpio/gpio.c:98,219–236` (override and raw-handler exclusion mask).
See [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
GPIO sections 9.4/9.5/9.11 and SPI section 12.3, and
[Arm PL022 TRM](https://documentation-service.arm.com/static/5e8e3bc7fd977155116a9361),
FIFO/interrupt sections 2.3/3.4. Earlier sections preserve the detailed audit.

### Why these symptoms occurred

Each restart discarded accumulated RX and refilled only the initial eight TX
entries. Repeated destruction explains zero RX accounting, final drain=0 and
no retained overrun evidence; it also prevents sustained IRQ service. Reset
intervals prevent a stable selected slave from returning the preloaded header.
Rare longer-lived intervals allow IRQ refill to queue all 40 bytes. A valid
packet already shifted to ESP can coexist with later destruction of Pico's
RX accounting. This explains the possibility of historical CRC-valid packets;
there was no simultaneous trace proving the precise timing of an individual
successful packet. `sent=40` proves queuing, not all bytes reaching the wire;
RX=39 alone cannot establish a full transfer. BSY also includes nonempty TX,
so it was never sufficient proof of external clock activity.

### Fix and why it is correct

`prepare()` resets and configures SPI1 only after confirming CS high, at initial
idle or following deselection. It clears stale TX/FIFO/partial-word state and
leaves the peripheral configured as a slave but disabled, TX empty and interrupts
masked. Empty TX must remain masked because its level interrupt can assert while
idle. At CS falling, eight TX entries are primed before SSE is enabled; details
of the separately discovered first-byte alignment issue and its hardware result
are recorded below.

CS and SCK have permanent **output-enable** DISABLE overrides, not input
inversions or disabled inputs. Even the reset default master interval cannot
make Pico drive either controller-owned wire. CS falling no longer resets,
disables or reconfigures SPI1: it only copies the latest published immutable
packet, preloads eight bytes and unmasks existing SPI sources. The unchanged
100 us controller setup allowance covers that bounded work; the existing 1 ms
high interval covers recovery. Preloading on the preceding rise would make
packets stale around PPS, so this intentionally preserves snapshot-at-fall.

A GP9-only SDK raw handler acknowledges explicit edge identity. It ignores
other GPIOs, preserves Arduino's PPS callback, rejects duplicate/stale falling
notifications without disturbing an active transfer, and rejects an ambiguous
rise+fall ending low until deselection. A duplicate rise while already ready
also does not reset. Ambiguous rejected selections increment `csfault` and may
not count as completed transactions; normal clean selections do. No NVIC
priority change, new delay, ISR printing, FIFO service limit change or protocol
change was made. Reset still provides deterministic cleanup after short/extra
or partial-byte aborts; draining RX alone cannot remove prefetched TX words.

GPIO can of course change after a sampled high; the controller's existing idle
contract bounds that race. Independent output suppression prevents reset-induced
CS/SCK drive regardless. Controllers violating setup/idle timing are not made
supported by this change.

### Production diagnostics and verification

Removed the linker observer, GPIO event counters, raw snapshots, IRQ source /
byte attribution / histogram / min-max diagnostics and temporary register stubs.
Kept original cumulative `tx/short/extra/over`, publisher `sync/skipped`, and added:

- `full`: completions with exactly 40 accounted RX bytes, including a completion
  that might independently have overrun set; not a wire-level CRC assertion.
- `csfault`: rejected duplicate/stale notifications or coalesced edge groups.
  Counts one per observed fault branch/group, not physical edges or resets.

`tx = short + full + extra` modulo counter wrap. Counters are bounded uint32,
read with interrupts briefly disabled and printed only once per minute. Normal
traffic should increase `tx` and `full` equally with all error counters flat.

All host tests pass. Transport coverage includes 700 duplicate low notifications
without reset or snapshot corruption, PPS coexistence, all 41 publication split
points, all 0–39-byte abort lengths, partial-byte aborts at bits 1–7, extra bytes,
overrun recovery, stalled CS, coalesced/stale events and boot with CS held low.
The model tests lifecycle invariants, not real electrical timing or NVIC latency.
`pio run -e pico2 -j1` succeeds: RAM 11504 bytes, flash 49376 bytes.
`git diff --check` passes. Linked production inspection confirms the raw GP9
handler/override setup and no diagnostic wrapper. ESP and protocol/TIME_SYNC
code are unchanged; `pico-spi.log` retains SHA256
`a68119d6be24f9cc991400dbf380f7f2ae6d2433186c544a57793e431040715e`.
No flash, commit or push was performed.

**Historical acceptance plan (completed below).** At implementation time, the
next test was expected to establish sustained full RX accounting and valid ACT1
packets. The actual results appear in “Final hardware acceptance” below; the
startup counters were nonzero and are preserved as measured.


## Follow-up: TX begins one byte late (resolved on hardware)

### Verified reset-storm correction

The prior production lifecycle correction has now passed its first real hardware
acceptance run, with unchanged ESP firmware, wiring, SPI mode/rate, protocol and
polling behavior. Two consecutive Pico summaries were:

```text
tx=587 short=0 extra=0 over=0 full=587 csfault=0
tx=1176 short=0 extra=0 over=0 full=1176 csfault=0
```

This verifies complete RX framing, no SHORT/extra/overrun/CS-fault counts, and no
observed reset/reselect storm under the tested load. TIME_SYNC continues. This
success is retained as a separate result; the TX alignment issue below is a new
failure and does not undo that verification.

### Symptom and evidence

ESP now receives a coherent packet shifted one byte later: `00 41 43 54 31 01
28 6F ...`, where ACT1 should begin `41 43 54 31`. It occurs consistently while
Pico accounts for exactly 40 received bytes and reports no framing/error counts.
ESP firmware is unchanged. The leading zero plus the following contiguous ACT1
bytes is consistent with the slave's first transmitted frame being zero and the
transaction payload beginning at the next frame. That establishes a TX alignment
symptom; it does not, by itself, identify which SSP internal register stage
supplied the zero.

### Source and hardware analysis

Before this follow-up, `prepare()` reset/reconfigured the SSP while deselected,
then set CR1 to slave+SSE while leaving TX FIFO empty. The CS falling-edge handler
later copied the immutable packet and filled up to eight FIFO entries, while SSE
was already set. The RP2350 SSP transmit FIFO feeds a serial transmit path. Its
TX output is separately tristated while a slave is deselected. The original code
therefore permitted an enabled, empty transmitter to reach the active CS
boundary before the transaction's first FIFO entries were written.

The RP2350 datasheet, section 12.3.4.3, explicitly permits priming the TX FIFO
with up to eight values while the PrimeCell SSP is disabled, then enabling the
SSP. Section 12.3.1 describes the slave TX output tristate when deselected; the
PL022 TRM describes SPH=1's first-edge/first-capture relationship. Together these
support keeping the configured slave disabled with an empty FIFO while idle,
then loading the new transaction before setting SSE. This prevents an empty
preselection shifter state from preceding the packet. The exact prior internal
source of the observed zero (shift register versus first selected edge before
servicing) was not directly captured; this change removes that race by using the
documented preload-before-enable sequence.

### Narrow correction

`prepare()` still resets and configures only while CS is high, preserving the
verified reset-storm fix and deterministic FIFO/partial-word recovery. It now
leaves CR1.MS=1 with SSE=0 while idle. On a clean falling edge, code latches the
latest immutable packet, writes the same initial eight bytes while SSE is off,
then sets CR1.SSE and enables the existing IRQ mask. The controller's existing
100 us CS-to-first-clock setup interval remains in force. No reset occurs on the
falling edge; OE clamps, explicit GPIO edge handling, IRQ priorities, mode,
rate, packet construction and TIME_SYNC are untouched.

Packet freshness remains snapshot-at-fall: the 40-byte packet is copied only
when CS asserts, so preloading does not use a prior transaction's snapshot. The
SSP's 8-entry FIFO is primed from that copy before enabling. Aborted/short/extra
transactions still reset and clear in `prepare()` after deselection.

### Verification status

Host tests now assert the initial transaction's eight TX entries are written
while SSE is clear and that SSE is set before the selected transaction proceeds.
The complete host suite, `git diff --check`, and `pio run -e pico2 -j1` passed
for this follow-up. The separate hardware result is recorded below; it must not
be conflated with Issue 1.

## Final hardware acceptance — both SPI failures resolved

### Issue 1 result: reset/CS feedback storm

The reset-storm root cause and lifecycle fix above were independently validated
before the alignment follow-up. Two consecutive one-minute Pico summaries were:

```text
tx=587  short=0 extra=0 over=0 full=587  csfault=0
tx=1176 short=0 extra=0 over=0 full=1176 csfault=0
```

This verified that the prior catastrophic SHORT/zero-response failure stopped:
all completed transactions were full, no extras/overruns/CS faults were observed,
and TIME_SYNC continued. This is the hardware result for Issue 1 only.

### Issue 2 result: first-byte TX alignment

After priming the FIFO with SSE disabled before enabling it, ESP observed correct
byte-zero alignment and repeatedly validated Protocol-v1 packets:

```text
PICO: tx=1378 packet=VALID rx[0:8]=41 43 54 31 01 28 6F 00 ascii="ACT1.(o."
PICO: tx=1427 packet=VALID rx[0:8]=41 43 54 31 01 28 6F 00 ...
PICO: tx=1476 packet=VALID rx[0:8]=41 43 54 31 01 28 6F 00 ...
```

ESP UI/status reported the Pico responding and qualified as selected authority,
UTC valid/locked, valid Pico packets, progressing packet/boundary/sync sequences,
and successful phase association. TIME_SYNC continued. Thus the zero prefix is
removed on hardware. The empty/stale first transmit stage remains the inferred
mechanism; no internal shifter capture was made. The exact first-byte correction
is separate from the reset-storm correction: Issue 1 fixed transaction survival;
Issue 2 fixed TX frame alignment.

Final Pico cumulative summaries (counts are **from boot**, so startup transients
are included):

```text
NET ... tx=529  short=19 extra=0 over=0 full=510  csfault=95 sync=58  skipped=0
NET ... tx=1105 short=19 extra=0 over=0 full=1086 csfault=95 sync=118 skipped=0
NET ... tx=1683 short=19 extra=0 over=0 full=1664 csfault=95 sync=178 skipped=0
```

The first-to-second interval added 576 `tx` and 576 `full`; the second-to-third
added 578 of each. `short` remained 19 and `csfault` remained 95 across both
intervals; `extra` and `over` stayed zero. These were startup/bring-up events,
not zero-from-boot results. In steady state, every additional completed
transaction accounted for 40 RX bytes, with no continuing short, extra, overrun
or CS-fault increments. `sync` advanced by 60 in each interval and `skipped`
remained zero.

The register/FIFO host model validated ordering and recovery. Actual ESP CRC,
packet validity, source qualification, phase association and hardware counters
outrank simulation for transport acceptance. The model did not reproduce the
analogue reset transient or directly observe the SSP shift register.
