#pragma once
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#define SPI_SSPIMSC_TXIM_BITS 8
#define SPI_SSPIMSC_RXIM_BITS 4
#define SPI_SSPIMSC_RTIM_BITS 2
#define SPI_SSPIMSC_RORIM_BITS 1
#define SPI_SSPRIS_RORRIS_BITS 1
#define SPI_SSPICR_RTIC_BITS 2
#define SPI_SSPICR_RORIC_BITS 1
#define SPI_SSPCR1_MS_BITS 4
#define SPI_SSPCR1_SSE_BITS 2
#define RESETS_RESET_SPI1_BITS 1
#define SPI1_IRQ 32
#define SPI_CPOL_0 0
#define SPI_CPHA_1 1
#define SPI_MSB_FIRST 0
#define GPIO_OUT 1
#define GPIO_FUNC_SPI 1
#define CHANGE 3
using uint=unsigned;
namespace fake {
struct Data {
    std::deque<uint8_t> tx,rx;
    void operator=(uint32_t b);
    operator uint32_t() {assert(!rx.empty());auto b=rx.front();rx.pop_front();return b;}
};
struct Hardware {uint32_t cr0=0,cr1=0,cpsr=0,imsc=0,ris=0,icr=0;unsigned partial_bits=0;Data dr;};
extern Hardware hw;
extern bool expect_prime;
extern unsigned prime_writes;
inline void Data::operator=(uint32_t b) {
    assert(tx.size()<8);
    if(expect_prime) {assert(!(hw.cr1&SPI_SSPCR1_SSE_BITS));--prime_writes;}
    tx.push_back(b);
}
extern bool irq_enabled;
extern uint32_t reset_count,reset_selected,unsafe_resets;
extern unsigned functions[30],oeover[30];
extern uint32_t pending[30],enabled[30],raw_mask;
extern void (*raw_handler)();
extern uint32_t now;
extern uint32_t time_reads,advance_on_read,advance_by;
extern bool levels[30];
extern void (*callbacks[30])();
extern void (*irq)();
extern unsigned rises;

}
static constexpr int spi1=1;
#define IO_IRQ_BANK0 21
#define GPIO_IRQ_EDGE_FALL 4
#define GPIO_IRQ_EDGE_RISE 8
#define GPIO_OVERRIDE_LOW 2
inline void gpio_add_raw_irq_handler_masked(uint32_t mask,void (*fn)()) {fake::raw_mask=mask;fake::raw_handler=fn;}
inline void gpio_set_irq_enabled(unsigned p,uint32_t events,bool on) {
    fake::pending[p]&=~events;
    if(on)fake::enabled[p]|=events;else fake::enabled[p]&=~events;
}
inline uint32_t gpio_get_irq_event_mask(unsigned p) {return fake::pending[p]&fake::enabled[p];}
inline void gpio_acknowledge_irq(unsigned p,uint32_t events) {fake::pending[p]&=~events;}
inline void gpio_set_oeover(unsigned p,unsigned value) {fake::oeover[p]=value;}
inline fake::Hardware* spi_get_hw(int) {return &fake::hw;}
inline bool spi_is_readable(int) {return !fake::hw.dr.rx.empty();}
inline bool spi_is_writable(int) {return fake::hw.dr.tx.size()<8;}
inline void reset_block(uint32_t) {
    ++fake::reset_count;
    if(!fake::levels[9])++fake::reset_selected;
    // Model the relevant reset hazard, not analogue pad timing: master defaults
    // can change a SPI-muxed input's output unless OE is independently clamped.
    for(unsigned p: {9u,10u})if(fake::functions[p]==GPIO_FUNC_SPI && fake::oeover[p]!=GPIO_OVERRIDE_LOW) {
        ++fake::unsafe_resets;fake::pending[9]|=12;
    }
    fake::hw=fake::Hardware{};
}
inline void unreset_block_wait(uint32_t) {}
inline void spi_init(int,unsigned) {fake::hw.cpsr=2;}
inline void spi_set_slave(int,bool) {}
inline void spi_set_format(int,unsigned,unsigned,unsigned,unsigned) {fake::hw.cr0=0x87;}
inline void irq_set_exclusive_handler(int,void (*fn)()) {fake::irq=fn;}
inline void irq_set_enabled(int irq,bool enabled) {if(int(SPI1_IRQ)==irq)fake::irq_enabled=enabled;}

inline void gpio_init(unsigned) {}
inline bool gpio_get(unsigned p) {return fake::levels[p];}
inline void gpio_put(unsigned p,bool b) {if(p==12 && b && !fake::levels[p])++fake::rises;fake::levels[p]=b;}
inline void gpio_set_dir(unsigned,unsigned) {}
inline void gpio_set_function(unsigned p,unsigned function) {fake::functions[p]=function;}
inline void gpio_pull_up(unsigned) {} // External CS level is controlled by the test.
inline void gpio_pull_down(unsigned p) {fake::levels[p]=false;}
inline unsigned digitalPinToInterrupt(unsigned p) {return p;}
inline void attachInterrupt(unsigned p,void (*fn)(),unsigned) {
    fake::callbacks[p]=fn;
    gpio_set_irq_enabled(p,12,true);
}
inline uint32_t time_us_32() {
    ++fake::time_reads;
    if(fake::advance_on_read && fake::time_reads==fake::advance_on_read)fake::now+=fake::advance_by;
    return fake::now;
}
inline uint32_t save_and_disable_interrupts() {return 0;}
inline void restore_interrupts(uint32_t) {}
