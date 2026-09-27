# Absurdly Accurate Clock
## Pico / RP2350 technical architecture

**First documentation pass · 25 September 2026**  
Source baseline: `5b3225f1cb6dd088bbebe10cbe837e0dcf2c7aef` in `/home/rob/Projects/absurdly-accurate-clock`.

### Why “absurdly accurate”?

The absurdity is the timing capability available for the money. This project combines a low-cost GPS receiver, a Raspberry Pi Pico 2, an inexpensive antenna and interface hardware, and a surplus vacuum-fluorescent display. Rob’s project account puts the receiver at roughly $12 and the thrift-store display at roughly $7. Those are reported component prices, not an audited bill of materials or a claim about the complete system cost.

The interesting result is that such modest hardware can distinguish a UTC second’s identity from its timing boundary, qualify the relationship between them, maintain an authoritative clock state, and expose that state to other equipment. The project gains access to satellite-derived time without building the infrastructure that makes it available. Much of the engineering is therefore in deciding what to trust, how to associate it, and what to do when that trust is no longer justified.

The name does not claim laboratory-leading accuracy. The present implementation has no characterized absolute timing-error budget, calibrated output delay, or demonstrated uncertainty relative to a laboratory reference. Its value is a useful timing architecture built from inexpensive parts, with enough explicit state and testable behavior to examine its claims critically.

## Documentation roadmap

This pass drafts the technical overview and system architecture, including the operating contracts needed to understand the whole Pico side. The later chapters below are the roadmap for deeper documentation; their listing does not imply that those chapters or planned features are complete.

| Chapter | Detailed contents | Status |
| --- | --- | --- |
| **1. Technical overview** | Purpose and cost premise; implemented capability; evidence terminology; scope and exclusions | Drafted below |
| **2. System architecture** | Hardware and functional boundaries; ownership of time; interrupt/main-loop execution; shared-resource limits; system invariants | Drafted below |
| **3. Authoritative time model** | UTC versus GPS system time; Unix epoch and calendar policy; boundary versus processing time; permitted corrections; leap-second limitations | Overview below; deeper treatment planned |
| **4. Acquisition and synchronization** | Receiver configuration contract; RMC validation; PPS capture; reception window; two-label qualification; continued verification; ambiguous and delayed input | Overview below; traces and complete state analysis planned |
| **5. Validity, failure, and recovery** | Meaning of each flag; startup; stale GPS; missing/glitching PPS; missed captures; UART backlog; conflicting labels; reacquisition; future holdover | Overview below; fault matrix and recovery traces planned |
| **6. Presentation and physical UI** | UTC-to-civil conversion; DST rules and scope; button behavior; rolling indicator; VFD serialization and caching; paired-HH accommodation; observable versus assumed delivery | Overview below; full presentation contract planned |
| **7. Network export and protocol** | Pico authority versus ESP32 transaction control; SPI electrical/framing contract; packet semantics and CRC; TIME_SYNC delay; edge/packet association; consumer reboot and loss policy; future NTP | Overview below; existing v1 specification remains the detailed reference |
| **8. Error sources and accuracy** | Receiver UTC/phase configuration; antenna/cable effects; PPS capture latency and jitter; timer quantization; loop delay; TIME_SYNC residual error; ESP32 capture and network asymmetry; measurement method and uncertainty | Planned; no numerical accuracy claim yet |
| **9. Verification and testing** | Requirements-to-test mapping; host models and their limits; independent protocol vectors; electrical captures; reference comparison; long-duration acquisition/loss tests; retained measurement records | Current evidence summarized below; full plan planned |
| **10. Hardware and construction** | Exact receiver/antenna identities and settings; costed BOM; power distribution and interlock if fitted; GPIO/header mapping; MAX3232 and DB9 wiring; grounds, logic levels, startup and power sequencing; expansion | Planned; verify against the actual assembly and Rev 2 drawing |
| **11. Design decisions** | UTC ownership; label/edge separation; invalidation instead of unqualified holdover; native SPI peripheral choice; Rev 1→Rev 2 wiring; fixed packet/64-bit epoch/CRC; bounded diagnostics; display accommodation | Rationale introduced below; dated decision records planned |
| **12. Source-code map** | Functional chapter → implementation → tests; platform/core versions; build and bench entry points | Small evidence map included now; full map comes last |

## 1. Technical overview

### 1.1 What the instrument currently does

The Pico receives GPS serial data and a separate pulse-per-second signal. It uses valid RMC sentences to name UTC seconds and qualifying PPS captures to advance the local second. Once their association has been established, it presents the time on a Posiflex PD-2200 and publishes a read-only time/status interface for a future network processor.

The Pico is the authoritative time source **within this system**. That means it owns the accepted epoch, its validity, and the decision to announce a new qualified boundary. It does not mean the Pico independently realizes UTC or can detect every error in its receiver. Its authority is architectural; the accuracy of its source and of its timing path remains a separate question.

The implementation includes GPS/PPS acquisition and reacquisition, separate quality flags, calendar conversion, a physical timezone button, a PPS-synchronized visual indicator, bounded display and diagnostic servicing, and a versioned SPI/TIME_SYNC export. The ESP32 network appliance and NTP service are future work. OCXO holdover is also future work: there is no oscillator control loop, frequency estimation, or uncertainty model providing time through a reference outage.

### 1.2 How to read the claims

An **implemented contract** describes behavior found in the inspected source, often exercised by host tests. It is conditional on the stated input and execution assumptions; it is not a calibrated physical guarantee. A **bench observation** records something reported from the assembled hardware. An **assumption** is a property needed by the design but not established by its internal checks. A **measured result** needs a stated method, reference, conditions, and uncertainty before it can support an accuracy specification. An **uncharacterized quantity** has no defensible numerical bound in this pass.

This distinction matters especially for “lock.” Here, lock means the software has accepted a consistent mapping between consecutive PPS captures and UTC labels. It does not mean an oscillator has been phase-locked by a servo, nor does it certify the edge’s error relative to UTC.

## 2. System architecture

### 2.1 Functional boundaries

```text
 GPS receiver
   │
   ├── NMEA / UART ──► validation + second-label association ──┐
   │                                                         │
   └── rising PPS ───► interrupt timestamp + sequence ─────────┤
                                                             ▼
                                             RP2350 authoritative state
                                             UTC epoch + validity/quality
                                                │                │
                         read-only presentation │                │ read-only export
                                                ▼                ▼
 Button ─► selected zone ─► civil-time conversion          coherent v1 packet
                            + frame rendering             + qualified TIME_SYNC
                                   │                             │
                            bounded UART output                  │ SPI1 peripheral
                                   │                             │
                               MAX3232                           ▼
                                   │                      Future ESP32
                              Posiflex VFD                SPI controller/consumer
                                                          network-time appliance
                                                                 │
                                                            Future LAN NTP
```

The main separation is between determining time and representing it. The display reads time and quality, converts a copy into civil time, and schedules output. The network publisher reads the same authoritative state and serializes it. Neither consumer gets a path for setting the Pico clock.

This arrangement lets a display fault remain a presentation fault and prevents future network policy from silently becoming a second clock-control mechanism. It also makes local-time selection irrelevant to the timestamp exported over SPI. These are data-ownership properties, not complete real-time isolation: all of this work still shares the processor, interrupts, and main-loop service time.

### 2.2 Execution and bounded work

The PPS interrupt records a microsecond timer reading, increments a capture sequence, and marks a pulse seen. It does not parse a sentence, convert a calendar, write the display, or format diagnostics. The main loop obtains a coherent, interrupt-protected copy of that capture and a current timer reading, then applies timebase policy.

Only the latest capture is retained. If multiple edges occur before processing, a sequence discontinuity is detected rather than silently treating the newest capture as the next second. This favors explicit loss of qualification over guessing how to recover missed timing events.

Normal service bounds GPS work to 64 received bytes per loop and USB diagnostic drainage to 64 bytes. The VFD serializer submits at most one byte per service call. A bounded USB queue drops complete diagnostic messages when it has insufficient space; an absent USB host does not become a reason to stop time acquisition. Startup does contain display initialization delays, so accumulated GPS bytes are deliberately drained before association begins.

Bounded work reduces interference but is not a measured worst-case execution-time proof. A gap of more than 20 ms between GPS-service passes is treated as loss of trustworthy reception timing: the parsers reset, lock is cleared, and buffered bytes are drained. Display or interrupt load therefore cannot change the epoch through an intended data path, but excessive load can cause availability loss. A future latency study must include the complete running system.

The RP2350 hardware watchdog provides unattended liveness recovery with a 4,000 ms
timeout. It is armed early in setup and fed only at the end of a completed main-loop
pass, after timing, diagnostics, network and VFD work; interrupt/timer paths never
feed it. Ordinary GPS/PPS loss, invalid UTC, display backpressure/faults and absent
ESP32 operation do not gate feeding. A wedged service can therefore cause recovery,
while degraded input quality by itself cannot.

Watchdog recovery uses normal startup and reacquisition, retaining no UTC/PPS or
TIME_SYNC validity. The RP2350-aware SDK reset-cause check runs before arming and
queues `RESET: watchdog` on existing USB diagnostics. Throughout that boot,
`RESET=WATCHDOG` is also queued after every fifth ordinary diagnostic message,
allowing observation after USB reconnects. This uses the same bounded queue and
drop policy, counts no supplemental markers, and introduces no timer; quiet
operation can space reports several minutes apart. Normal boots omit the marker.
For that boot only, heartbeat
toggles are 250 ms instead of 500 ms, persisting through reacquisition until a
normal reset/power cycle. This diagnostic is separate from time/network quality.
See the README's unattended recovery section for debugger bench verification.

### 2.3 Architectural invariants and their scope

| Invariant in the current software | Practical meaning |
| --- | --- |
| Only processing a new qualifying PPS capture commits or advances authoritative UTC | An RMC arrival, display animation, button press, or SPI read cannot advance the second |
| Civil time is derived from UTC at the presentation boundary | DST and timezone selection cannot alter exported UTC |
| Loss of qualification clears UTC validity | A retained old epoch is not presented as valid running time |
| An SPI transaction receives one immutable published snapshot | A read cannot splice fields from two publications |
| MOSI data is discarded | The v1 consumer has no command for disciplining or setting Pico time |
| TIME_SYNC requires a newly processed valid boundary within the age limit | Packet availability and timing-edge validity are separate properties |

These contracts do not establish correct receiver configuration, accurate electrical timestamps, or reliable optical display output. Those require evidence at the corresponding physical boundaries.

## 3. Authoritative UTC model

The internal epoch is a signed 64-bit count of Unix seconds, accompanied by a UTC calendar representation. It is used as UTC civil labeling, not as a count of GPS system-time seconds. The firmware relies on the receiver’s RMC UTC report; it does not independently derive the GPS-to-UTC offset.

The epoch identifies the second beginning at the accepted PPS boundary. The main loop updates the stored state after capturing that boundary, so a software observation of the new epoch necessarily includes processing latency. The implementation does not maintain a continuously interpolated, calibrated fractional UTC clock between pulses. Microsecond elapsed time supports qualification, animation, and delivery-delay reporting; timer resolution alone is not an accuracy specification.

RMC parsing assigns two-digit years to 2000–2099 and rejects second 60. Unix conversion does not represent leap-second insertion or deletion. A signed 64-bit wire field avoids a 32-bit 2038 restriction, but does not enlarge the parser’s century policy or make the calendar helpers valid over the entire signed range. The host tests explicitly record a negative-epoch conversion limitation. Leap handling and broader calendar-domain support need separate design work.

Following a conflicting label, the clock can reacquire and commit a corrected epoch at a subsequent edge. There is no slew algorithm or monotonic-time guarantee across loss and reacquisition. Consumers must treat loss of validity and renewed association as meaningful events rather than assuming an uninterrupted timescale.

## 4. NMEA, PPS, and association

### 4.1 Why there are two inputs

NMEA provides the label: which UTC date and second the receiver reports. PPS supplies the recurring boundary to which a label can be attached. Setting the running clock when a UART sentence finishes would instead attach the second to message delivery, including receiver scheduling and serial transmission delay.

Separating the two inputs avoids that particular error, but creates an association problem. The current receiver contract is a 1 Hz navigation solution with a rising PPS aligned to the whole UTC second and an RMC sentence for that second following its pulse. Firmware does not configure the receiver or interrogate that contract. It must be verified for the actual receiver and settings.

The repository records synchronization with RMC completion approximately 140 ms after the preceding PPS. That is a useful bench observation supporting the chosen reception window. It is not a calibrated latency result and cannot rule out a stream consistently carrying a label one whole second late. Two internally consistent labels can still have the same absolute offset error.

### 4.2 Qualification and commitment

The time parser accepts checksum-valid `GPRMC`, active status `A`, and valid time/date fields. `GNRMC` is not currently recognized. Damaged checksums and unrecognized sentences are ignored; a recognized, checksum-valid RMC with invalid status or fields explicitly invalidates GPS time. A time such as `.00` is eligible for association, while a nonzero fractional component is not.

Before a label can be associated, the captured edge cadence must qualify: consecutive sequence numbers and an interval from 900 to 1100 ms. The sentence’s `$` must be consumed at least 20 ms after the edge; completion must be no later than 900 ms after it; and the capture sequence must remain unchanged across reception. These are software-consumption timestamps, not hardware timestamps on each UART byte. The servicing-gap rule is an explicit defense against known backlog, not proof of zero reception latency.

Two usable labels on consecutive captures must increase by exactly one UTC second. The following qualifying edge commits the second label plus one. Sentence reception itself never writes a new authoritative epoch.

```text
 Acquisition example — time runs left to right; spacing is illustrative
 Precondition: preceding captures have established acceptable PPS cadence.

 PPS capture         ↑ A                    ↑ B                    ↑ C
 RMC consumed           [label T]               [label T+1]
 Association            candidate               confirmed pair
 Authoritative UTC      unavailable             unavailable         T+2 committed
                                                                   on processing C

 Each label: start ≥20 ms and completion ≤900 ms after its associated capture;
 no intervening capture may occur within that sentence.
```

Once locked, qualifying edges advance the epoch even between RMC deliveries, while fresh coherent labels continue checking and reanchoring it. A usable label that disagrees with the current second clears lock; the old stored epoch is left unchanged until reacquisition. Conflicting duplicate labels also break qualification. An unusable association clears the pending candidate pair; it need not immediately clear an existing lock, which remains subject to the association freshness timeout.

The generous cadence and reception windows are rejection rules, not error tolerances. Passing a ±100 ms cadence window does not mean the clock is specified to ±100 ms, or that it has measured the PPS to any finer bound.

## 5. Validity, quality, and recovery

The system keeps separate answers to separate questions. A receiver can deliver a plausible UTC message without PPS. A PPS input can keep pulsing while serial time is invalid. Neither condition alone is enough to publish qualified UTC.

| State | Meaning in this implementation | What it does not establish |
| --- | --- | --- |
| `GPS_VALID` | A parser-accepted RMC remains fresh | A usable PPS association or certified receiver accuracy |
| `PPS_PRESENT` | A capture is present and less than 1.5 s old | Valid cadence, UTC identity, or phase accuracy |
| `PPS_LOCKED` | The timebase has accepted the label/capture association and retained qualification | Oscillator servo lock or laboratory traceability |
| `UTC_VALID` | The stored epoch may currently be used as authoritative UTC | Guaranteed continuity through future faults |
| `SAT_VALID` | Supplemental GGA satellite-use count is fresh and valid | A timing uncertainty estimate |
| `SYNC_VALID` | The exported current boundary/epoch has a corresponding emitted TIME_SYNC edge | Zero delivery delay or a calibrated phase output |
| `HOLDOVER` | Reserved; always clear in the current publisher | Any implemented outage capability |

In the present timebase, `PPS_LOCKED` and `UTC_VALID` are asserted and cleared together. Keeping separate concepts in the interface avoids requiring every future time source to share that implementation detail.

Missing PPS clears presence and UTC qualification at 1.5 s. A skipped sequence or an out-of-window interval clears lock when processed. A recognized invalid RMC clears GPS validity and lock immediately. Three seconds without a valid RMC clears GPS validity and lock; three seconds without a usable association also clears lock, even if parseable GPS time continues. Timeout enforcement occurs when the main loop polls, not through independent deadline interrupts.

On loss, the internal last epoch remains stored but is no longer authoritative. The VFD shows unavailable time; the network packet clears UTC validity and uses a zero epoch sentinel. That zero means “unavailable,” not a valid date in 1970. TIME_SYNC emits no new edge for invalid time. Recovery follows qualification again, with a subsequent accepted boundary required before time export resumes.

There can be a brief freshness allowance in which PPS advances the clock after the most recent RMC, but this is not OCXO holdover. There is no extrapolated running UTC once PPS disappears and no characterized drift allowance during a prolonged reference outage.

## 6. Presentation, VFD, and UI

The physical button cycles UTC, Eastern, Central, Mountain, Pacific, and back to UTC. It uses an active-low input with a pull-up, 30 ms debounce, and release-before-rearm behavior. Holding the button does not repeat, and reset returns to UTC. Its only state-changing role is selection of the presentation zone.

Civil conversion applies the implemented contemporary U.S. DST rule: the second Sunday in March and first Sunday in November, with transitions interpreted from local 02:00 using the preceding offset. This is a deliberately limited rule implementation, not a timezone database covering historical rules, regional exceptions, or future legislation. The document describes the code’s policy rather than asserting that it is suitable for every locality named by a broad region.

The upper VFD row shows the selected abbreviation and `HH:MM:SS.X`. The lower row shows GPS validity, PPS presence, and a satellites-used count from valid `GPGGA` or `GNGGA`. The count expires after three seconds and has no authority over UTC. A `PPS+` indication means presence, not lock; both status plus signs can coexist with unavailable time if association fails.

The final digit is a **PPS-synchronized rolling decade indicator**, not decimal tenths of UTC. It holds zero for the first 500 ms after the captured edge, then advances in 50 ms steps, reaches nine at 900 ms, and holds until the next pulse. Missing phases are skipped rather than queued. Its visible phase includes display transmission and device response, so it cannot serve as evidence of fractional-second accuracy.

The VFD path uses 9600-baud, 8N1 UART through a MAX3232 to the separately powered Posiflex. Runtime output changes occupied cells instead of redrawing entire rows. The startup clear establishes blank cells that are subsequently left unwritten, accommodating the recorded space-to-zero behavior of this unit.

Hour digits receive a further accommodation: either dirty hour cell causes a contiguous two-character write starting at address 07, avoiding a direct address-08 hour-ones update. Once the first hour payload is accepted, the pair is frozen until completion; a later desired hour generates another update. This is the current implementation, superseding older notes describing exclusively single-character runtime writes.

The output cache means **accepted by the UART API**, not observed on the display. Reported physical `00→08` and `10→18` symptoms have not been assigned a proven root cause. Host tests support the serializer’s correctness and the intended paired-hour byte stream, but do not prove that the physical accommodation cures the unit. UART transport, display interpretation, and optical output remain separate evidence boundaries. Multi-character screen transitions are not atomic.

## 7. SPI/TIME_SYNC and the network boundary

### 7.1 Time authority and transaction control

The RP2350 is the SPI1 **peripheral** and the future ESP32 is the SPI **controller**. The ESP32 supplies clock and chip select because it initiates reads. That gives it control of bus transactions, not authority over time. The Pico decides the epoch, quality flags, and whether a TIME_SYNC edge is justified.

The recorded design rationale for reversing the earlier controller-oriented arrangement was compatibility with native SPI1’s fixed RX/TX pin roles. PIO could have preserved a different arrangement, but would have added transport machinery. The present source confirms use of native SPI1, with GP8 as Pico RX/MOSI, GP9 as CS, GP10 as SCK, GP11 as Pico TX/MISO, and GP12 as TIME_SYNC. GP13 is reserved and uninitialized. All directions are relative to the Pico.

MOSI carries dummy data and is discarded. No received command can set time, change qualification, or discipline the Pico. The intended ESP32 role is to consume qualified source time and implement network service in a separate future project. Any future upstream-network fallback policy belongs on that appliance and must not be mistaken for a path back into Pico authority.

### 7.2 Coherent packet transport

Protocol v1 exports a fixed 40-byte packet with signature, version, length, quality flags, publication sequence, PPS boundary sequence, signed 64-bit epoch, TIME_SYNC sequence and delay, satellite count, reserved bytes, and CRC-32/ISO-HDLC. Integer fields are explicitly serialized little-endian. The CRC detects corruption; it does not authenticate a source.

The publisher constructs a complete inactive buffer and publishes it atomically. At CS assertion, the transport copies the selected packet into a transaction buffer that remains unchanged for the read. This is why a consumer can receive a coherent older snapshot while a new second is being published. It must examine sequences and validity rather than assuming that the act of reading created a fresh second.

The current contract uses mode 1, MSB-first, eight-bit words, at no more than 100 kHz pending bench qualification. A controller waits one second after Pico startup, allows at least 100 µs from CS assertion to clocks, reads exactly 40 bytes, and holds CS at least 10 µs after the final trailing clock edge. It keeps CS high at least 1 ms between transactions and limits requests to ten per second. These are integration requirements, not timings demonstrated by the host register model.

Each new assertion resets peripheral framing and FIFO state. Short, extra, and aborted transfers cannot mutate the timebase. A stalled controller does not produce a software wait loop, and excess received bytes eventually disable transport interrupts until CS rises. This contains several transport failure modes without claiming immunity to arbitrary electrical faults or interrupt load.

### 7.3 What TIME_SYNC actually means

TIME_SYNC is a software-delivered announcement of a newly processed authoritative boundary. It rises only when UTC is valid, PPS is present and locked, and the elapsed time from the captured PPS timestamp is at most 5000 µs. A late boundary suppresses the edge instead of presenting it as timely. It returns low at the first service call at least 100 µs later; pulse width is not a precision parameter.

The publisher commits the matching boundary/epoch identity, delay and emitted-edge sequence only after the final timer check passes and the GPIO rising write occurs. If that final read crosses 5000 µs, the edge is suppressed and the skipped counter increments. The current authoritative boundary is still published, but `SYNC_VALID` is clear and `sync_delay_us` is the invalid sentinel; `sync_sequence` remains the cumulative count of edges actually emitted. Thus an accepted clock boundary is not by itself evidence that its TIME_SYNC edge was emitted.

```text
 Electrical GPS PPS       ↑
 PPS interrupt timestamp    | capture
 Main-loop acceptance             | qualified UTC update
 TIME_SYNC                         ↑ software GPIO write
 Packet publication                 | complete snapshot becomes available
 ESP32 read (future)                      [ CS ... 40 bytes ... ]
                                     wait ≥1 ms after TIME_SYNC before CS

 Reported sync_delay_us spans capture timestamp → timer read just before GPIO.
 It does not span the original electrical PPS edge → calibrated output edge.
```

The intended consumer estimates the boundary time in its own capture domain as:

`estimated boundary = captured TIME_SYNC time − reported sync_delay_us`

That subtraction removes the reported source processing interval. It still retains source interrupt timestamp latency, timer quantization and scale error, the interval from the final timer read to GPIO transition, and consumer capture uncertainty. None of those residuals has a characterized combined laboratory bound here. The 5 ms gate is an acceptance policy, not a ±5 ms accuracy specification.

TIME_SYNC is not a packet-ready strobe. The v1 consumer procedure waits 1 ms after the edge before asserting CS, checks `SYNC_VALID`, `UTC_VALID`, `PPS_LOCKED`, and `PPS_PRESENT`, and matches the synchronization and boundary sequences. An old packet may require another read. A read overlapping another edge must be associated explicitly or discarded as ambiguous. Missed edges, sequence discontinuities, reboot, invalid flags, or no edge for 1.5 s invalidate the consumer’s phase association. These are requirements for the future ESP32, not evidence that ESP32 firmware already implements them.

The detailed byte layout and transaction rules remain in the repository’s `docs/clock-network-protocol.md`. There is currently no measured end-to-end NTP accuracy or functioning ESP32 NTP service established by this Pico-side pass.

## 8. Evidence, limitations, and the next engineering questions

All eight executables in the current host runner passed during this documentation pass. They exercise the production parser/timebase, calendar and presentation logic, VFD serialization under stalls and changing desired output, timezone/button behavior, packet encoding and validation, and native SPI transport compiled against a register model. Tests include independently generated packet vectors, corruption rejection, and immutable transaction snapshots. No firmware upload, new hardware measurement, or target firmware build was performed for this documentation-only pass.

| Claim or observation | Evidence available now | Remaining boundary |
| --- | --- | --- |
| PPS-driven epoch and qualified RMC association | Source inspection and passing host tests | Actual receiver pulse grid, polarity, UTC label alignment, and local capture delay |
| Approximately +140 ms RMC completion after preceding PPS | Previously recorded bench observation | Independent absolute-second validation and retained measurement conditions |
| Display conversion cannot set UTC | Separate data path and integration tests | Shared CPU load can still affect service availability |
| Contiguous hour writes and correct software cache accounting | Current serializer, rendered-screen tests and stress cases | Physical hour symptom and effectiveness of the accommodation |
| Coherent CRC-protected SPI packet | Encoder tests, independent vectors and transport model | Real controller framing, electrical integrity, abort behavior and timing margins |
| Qualified, delay-reported TIME_SYNC | Source inspection and register-model tests | Actual phase offset, jitter, timer scale, and consumer capture error |
| Low hardware investment | Rob’s reported component prices and use of surplus parts | Full costed BOM, including power, antenna and interconnect |

The first accuracy work should establish the identity and configuration of the receiver, then verify absolute second association against an independent reference. A consistently wrong whole-second label would survive the internal consistency checks. Once that is resolved, capture the physical PPS input and TIME_SYNC output together under realistic UART, display, SPI, and diagnostic activity. Record offset distributions, outliers, skipped outputs, reference uncertainty, and test conditions rather than promoting the best observed trace into a specification.

The next integration work should exercise protocol v1 with an actual SPI controller, including early clocks, short and partial-byte transfers, extra clocks, held CS, and transactions spanning publication. The future ESP32 then needs explicit policies for stale source data, phase loss, reboot, and network timestamping. Accurate local source time alone does not bound network-client error.

OCXO holdover should enter as a separate design with oscillator characterization, frequency/phase estimation, outage age, uncertainty growth, and reacquisition policy. Merely keeping the last second running would not justify the existing reserved `HOLDOVER` flag. Leap-second behavior likewise needs a defined source-to-network contract before it can be claimed.

## 9. Evidence map and maintenance notes

Paths below are relative to the inspected repository. This compact map makes claims auditable without making source files the organizing principle of the architecture.

| Subject | Implementation inspected | Main tests / existing records |
| --- | --- | --- |
| UTC ownership, association, freshness | `src/clock_state.cpp`, `include/clock_state.hpp`, `src/main.cpp` | `test/host/timebase_test.cpp`; `docs/pps-timebase.md` |
| RMC and supplementary GGA | `src/nmea_rmc.cpp`, `src/nmea_gga.cpp` | `test/host/timebase_test.cpp` |
| Civil presentation and UI | `src/display_time.cpp`, `src/zone_button.cpp`, `src/clock_display.cpp` | `test/host/timezone_test.cpp`; `docs/display-timezone.md` |
| VFD scheduling and hour accommodation | `src/clock_vfd.cpp` | `vfd_test.cpp`, `hh_test.cpp`, `hh_zero_test.cpp`, `hh_pair_test.cpp` under `test/host/`; `docs/hh-zero-investigation.md` |
| Packet publication and validation | `src/network_protocol.cpp` | `test/host/network_test.cpp`; `test/host/network-v1-vectors.json` |
| Native SPI and TIME_SYNC | `src/network_interface.cpp` | `test/host/network_transport_test.cpp`; `docs/clock-network-protocol.md` |
| Pin and UART contract | `include/pins.hpp`, `src/hardware.cpp`, `src/main.cpp` | `README.md`; Rev 2 pin drawing in `Paper-Documents/` |

The source checkout was clean at inspection. This document was created separately; existing source, documentation, and synced project material were preserved. The earlier conversation supplied the cost premise and historical design intent, while behavioral claims were checked against this source baseline and its tests. The hardware drawing is listed as a reference for the later hardware chapter; this pass verified pin assignments against source and the current protocol specification, not by a fresh inspection of the assembly.

The historical HH investigations preserve their original findings and identify the earlier renderer/serializer descriptions as superseded. Current presentation and output behavior are documented in `docs/pps-timebase.md` and `docs/display-timezone.md`; current TIME_SYNC publication semantics are reflected here and in `docs/clock-network-protocol.md`.

As the architecture develops, update the relevant functional chapter when a behavior or boundary changes, and record consequential choices while their rationale is still available. Accuracy claims should carry their measurement record; implementation claims should identify their source baseline. That keeps the document useful both for presenting the design and for deciding where informed criticism calls for a change.
