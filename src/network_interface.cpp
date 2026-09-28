#include "network_interface.hpp"
#include "network_protocol.hpp"
#include "pins.hpp"
#include <Arduino.h>
#include <cstdio>
#include <hardware/spi.h>
#include <hardware/irq.h>
#include <hardware/gpio.h>
#include <hardware/resets.h>
#include <hardware/sync.h>
#include <hardware/timer.h>

namespace clock_network {
namespace {
Mailbox mailbox;
Publisher publisher;
Packet transaction;
volatile uint32_t transactions=0,short_reads=0,extra_reads=0,overruns=0;
volatile uint32_t full_reads=0,cs_faults=0;
unsigned sent=0,received=0;
bool active=false,ready=false,overrun=false;
bool sync_high=false;
uint32_t sync_at=0;
uint32_t cr0=0,cpsr=0;
constexpr uint32_t cs_edges=GPIO_IRQ_EDGE_FALL|GPIO_IRQ_EDGE_RISE;
constexpr uint32_t irq_mask=SPI_SSPIMSC_TXIM_BITS|SPI_SSPIMSC_RXIM_BITS|
                            SPI_SSPIMSC_RTIM_BITS|SPI_SSPIMSC_RORIM_BITS;
// Eight FIFO entries per pass; never wait for external clocks or a peer.
void drain() {
    if(spi_get_hw(spi1)->ris & SPI_SSPRIS_RORRIS_BITS)overrun=true;
    for(unsigned i=0;i<8 && spi_is_readable(spi1);++i) {
        const uint32_t ignored=spi_get_hw(spi1)->dr;
        (void)ignored; // Explicit volatile read discards MOSI; it is never a command.
        if(received<packet_size+8)++received;
    }
    spi_get_hw(spi1)->icr=SPI_SSPICR_RTIC_BITS|SPI_SSPICR_RORIC_BITS;
}
void fill() {
    for(unsigned i=0;i<8 && spi_is_writable(spi1);++i) {
        spi_get_hw(spi1)->dr=sent<packet_size ? transaction[sent]:0;
        if(sent<packet_size)++sent;
    }
}
// Only after deselection (or initial idle). Reset removes prefetched TX bytes
// and aborted partial words. GPIO OE overrides keep CS/SCK inputs even while
// reset temporarily restores master mode. Never reset in the falling-edge path.
void prepare() {
    ready=false;
    if(!gpio_get(pins::esp_spi_cs))return;
    spi_get_hw(spi1)->imsc=0;
    reset_block(RESETS_RESET_SPI1_BITS);
    unreset_block_wait(RESETS_RESET_SPI1_BITS);
    spi_get_hw(spi1)->cr0=cr0;spi_get_hw(spi1)->cpsr=cpsr;
    spi_get_hw(spi1)->cr1=SPI_SSPCR1_MS_BITS|SPI_SSPCR1_SSE_BITS;
    ready=true;
    // Empty TX, IRQs masked: no stale packet and no idle TX interrupt storm.
}
void spiInterrupt() {
    if(!active) {spi_get_hw(spi1)->imsc=0;return;}
    drain();
    if(received>=packet_size+8) {spi_get_hw(spi1)->imsc=0;return;}
    fill();
}
void csInterrupt() {
    const uint32_t events=gpio_get_irq_event_mask(pins::esp_spi_cs)&cs_edges;
    if(!events)return; // Raw bank handler may also run for PPS/other GPIOs.
    gpio_acknowledge_irq(pins::esp_spi_cs,events);
    const bool high=gpio_get(pins::esp_spi_cs);
    if(events&GPIO_IRQ_EDGE_RISE) {
        if(events&GPIO_IRQ_EDGE_FALL)++cs_faults; // Edges coalesced; boundary ambiguous.
        if(!high) {
            // A new selection already started before its preceding rise was
            // handled. Reject it; wait for high to recover, never reset under CS.
            if(!(events&GPIO_IRQ_EDGE_FALL))++cs_faults;
            spi_get_hw(spi1)->imsc=0;
            spi_get_hw(spi1)->cr1=SPI_SSPCR1_MS_BITS;
            active=ready=false;
            return;
        }
        if(active) {
            spi_get_hw(spi1)->imsc=0;
            drain();++transactions;
            if(received<packet_size)++short_reads;
            if(received==packet_size)++full_reads;
            if(received>packet_size)++extra_reads;
            if(overrun)++overruns;
            active=false;
        } else if(ready) {
            ++cs_faults; // Duplicate rise: already clean, do not prepare again.
            return;
        }
        prepare();
        return;
    }
    if(high || active || !ready) {
        ++cs_faults; // Duplicate/stale fall cannot destroy an active frame.
        return;
    }
    transaction=mailbox.latch();sent=received=0;overrun=false;
    ready=false;active=true;
    fill(); // Existing >=100 us CS setup contract covers latch/preload only.
    spi_get_hw(spi1)->imsc=irq_mask;
}
}
void begin() {
    static_assert(ATOMIC_CHAR_LOCK_FREE==2,"Packet index must be lock free");
    gpio_init(pins::time_sync);gpio_put(pins::time_sync,0);gpio_set_dir(pins::time_sync,GPIO_OUT);
    publisher.update({},{});mailbox.publish(encode(publisher.snapshot()));
    spi_init(spi1,100000);
    spi_set_slave(spi1,true);
    spi_set_format(spi1,8,SPI_CPOL_0,SPI_CPHA_1,SPI_MSB_FIRST);
    cr0=spi_get_hw(spi1)->cr0;cpsr=spi_get_hw(spi1)->cpsr;
    spi_get_hw(spi1)->imsc=0;
    spi_get_hw(spi1)->cr1=SPI_SSPCR1_MS_BITS;
    gpio_set_function(pins::esp_spi_rx,GPIO_FUNC_SPI);
    gpio_set_function(pins::esp_spi_tx,GPIO_FUNC_SPI);
    gpio_set_function(pins::esp_spi_sck,GPIO_FUNC_SPI);
    gpio_set_oeover(pins::esp_spi_sck,GPIO_OVERRIDE_LOW);
    gpio_set_function(pins::esp_spi_cs,GPIO_FUNC_SPI);
    gpio_set_oeover(pins::esp_spi_cs,GPIO_OVERRIDE_LOW);
    gpio_pull_up(pins::esp_spi_cs);
    gpio_pull_down(pins::esp_spi_rx);gpio_pull_down(pins::esp_spi_sck);
    irq_set_exclusive_handler(SPI1_IRQ,spiInterrupt);
    irq_set_enabled(SPI1_IRQ,true);
    // Claim only GP9; preserve the SDK/Arduino default callback for PPS on GP2.
    const uint32_t saved=save_and_disable_interrupts();
    gpio_add_raw_irq_handler_masked(1u<<pins::esp_spi_cs,csInterrupt);
    gpio_set_irq_enabled(pins::esp_spi_cs,cs_edges,true);
    irq_set_enabled(IO_IRQ_BANK0,true);
    prepare(); // If CS is low at boot, leave disabled until its first rise.
    restore_interrupts(saved);
}
void service(const clock_model::State& state,const clock_model::Pulse& pulse) {
    uint32_t now=time_us_32();
    if(sync_high && uint32_t(now-sync_at)>=100) {
        gpio_put(pins::time_sync,0);sync_high=false;
    }
    if(publisher.needsSync(state,pulse,now)) {
        // Recheck the deadline immediately before emission. Only an actual edge
        // commits this boundary's synchronization identity into the packet.
        const uint32_t saved=save_and_disable_interrupts();
        now=time_us_32();
        if(uint32_t(now-pulse.at_us)<=5000) {
            gpio_put(pins::time_sync,1);sync_high=true;sync_at=now;
            publisher.emitted(pulse.sequence,now-pulse.at_us,state.utc_seconds);
        } else publisher.suppressed();
        restore_interrupts(saved);
    }
    if(publisher.update(state,pulse))mailbox.publish(encode(publisher.snapshot()));
}
bool diagnostic(char* buffer,size_t size,uint32_t now) {
    static bool initial=true;static uint32_t last=0;
    if(initial) {
        initial=false;last=now;
        snprintf(buffer,size,"NET v1 SPI1 peripheral mode=1 bytes=40 max_hz=100000 sync=qualified-delayed\r\n");
        return true;
    }
    if(uint32_t(now-last)<60000)return false;
    last=now;
    const uint32_t saved=save_and_disable_interrupts();
    const uint32_t tx=transactions,shorts=short_reads,extras=extra_reads,over=overruns;
    const uint32_t full=full_reads,faults=cs_faults;
    restore_interrupts(saved);
    const auto& s=publisher.snapshot();
    snprintf(buffer,size,"NET seq=%lu epoch=%lld flags=%02X tx=%lu short=%lu extra=%lu over=%lu full=%lu csfault=%lu sync=%lu skipped=%lu\r\n",
        static_cast<unsigned long>(s.sequence),static_cast<long long>(s.epoch),unsigned(s.flags),
        static_cast<unsigned long>(tx),static_cast<unsigned long>(shorts),static_cast<unsigned long>(extras),
        static_cast<unsigned long>(over),static_cast<unsigned long>(full),static_cast<unsigned long>(faults),
        static_cast<unsigned long>(s.sync_sequence),static_cast<unsigned long>(publisher.skipped()));
    return true;
}
}
