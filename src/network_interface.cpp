#include "network_interface.hpp"
#include "network_protocol.hpp"
#include "pins.hpp"
#include <Arduino.h>
#include <cstdio>
#include <hardware/spi.h>
#include <hardware/irq.h>
#include <hardware/resets.h>
#include <hardware/sync.h>
#include <hardware/timer.h>

namespace clock_network {
namespace {
Mailbox mailbox;
Publisher publisher;
Packet transaction;
volatile uint32_t transactions=0,short_reads=0,extra_reads=0,overruns=0;
unsigned sent=0,received=0;
bool active=false,overrun=false;
bool sync_high=false;
uint32_t sync_at=0;
uint32_t cr0=0,cpsr=0;
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
void spiInterrupt() {
    if(!active) {spi_get_hw(spi1)->imsc=0;return;}
    drain();
    if(received>=packet_size+8) {spi_get_hw(spi1)->imsc=0;return;}
    fill();
}
void csInterrupt() {
    if(gpio_get(pins::esp_spi_cs)) {
        spi_get_hw(spi1)->imsc=0;
        if(active) {
            drain();++transactions;
            if(received<packet_size)++short_reads;
            if(received>packet_size)++extra_reads;
            if(overrun)++overruns;
        }
        active=false;
        // Do not poll BSY: a stalled controller or partial byte must never block.
        spi_get_hw(spi1)->cr1=SPI_SSPCR1_MS_BITS;
        return;
    }
    // Hard peripheral reset clears both FIFOs AND an aborted partial word.
    // CS-to-first-clock setup in the protocol covers this fixed local work.
    spi_get_hw(spi1)->imsc=0;
    reset_block(RESETS_RESET_SPI1_BITS);
    unreset_block_wait(RESETS_RESET_SPI1_BITS);
    spi_get_hw(spi1)->cr0=cr0;spi_get_hw(spi1)->cpsr=cpsr;
    spi_get_hw(spi1)->cr1=SPI_SSPCR1_MS_BITS;
    transaction=mailbox.latch();sent=received=0;overrun=false;active=true;
    fill();
    spi_get_hw(spi1)->cr1=SPI_SSPCR1_MS_BITS|SPI_SSPCR1_SSE_BITS;
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
    gpio_set_function(pins::esp_spi_cs,GPIO_FUNC_SPI);
    gpio_pull_up(pins::esp_spi_cs);
    gpio_pull_down(pins::esp_spi_rx);gpio_pull_down(pins::esp_spi_sck);
    irq_set_exclusive_handler(SPI1_IRQ,spiInterrupt);
    irq_set_enabled(SPI1_IRQ,true);
    attachInterrupt(digitalPinToInterrupt(pins::esp_spi_cs),csInterrupt,CHANGE);
    // If CS was already low at boot, wait for high then a new assertion.
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
    restore_interrupts(saved);
    const auto& s=publisher.snapshot();
    snprintf(buffer,size,"NET seq=%lu epoch=%lld flags=%02X tx=%lu short=%lu extra=%lu over=%lu sync=%lu skipped=%lu\r\n",
        static_cast<unsigned long>(s.sequence),static_cast<long long>(s.epoch),unsigned(s.flags),
        static_cast<unsigned long>(tx),static_cast<unsigned long>(shorts),static_cast<unsigned long>(extras),
        static_cast<unsigned long>(over),static_cast<unsigned long>(s.sync_sequence),
        static_cast<unsigned long>(publisher.skipped()));
    return true;
}
}
