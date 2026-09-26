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
#define SPI1_IRQ 1
#define SPI_CPOL_0 0
#define SPI_CPHA_1 1
#define SPI_MSB_FIRST 0
#define GPIO_OUT 1
#define GPIO_FUNC_SPI 1
#define CHANGE 3
namespace fake {
struct Data {
    std::deque<uint8_t> tx,rx;
    void operator=(uint32_t b) {assert(tx.size()<8);tx.push_back(b);}
    operator uint32_t() {assert(!rx.empty());auto b=rx.front();rx.pop_front();return b;}
};
struct Hardware {uint32_t cr0=0,cr1=0,cpsr=0,imsc=0,ris=0,icr=0;Data dr;};
extern Hardware hw;
extern uint32_t now;
extern bool levels[30];
extern void (*callbacks[30])();
extern void (*irq)();
extern unsigned rises;
}
static constexpr int spi1=1;
inline fake::Hardware* spi_get_hw(int) {return &fake::hw;}
inline bool spi_is_readable(int) {return !fake::hw.dr.rx.empty();}
inline bool spi_is_writable(int) {return fake::hw.dr.tx.size()<8;}
inline void reset_block(uint32_t) {fake::hw=fake::Hardware{};}
inline void unreset_block_wait(uint32_t) {}
inline void spi_init(int,unsigned) {fake::hw.cpsr=2;}
inline void spi_set_slave(int,bool) {}
inline void spi_set_format(int,unsigned,unsigned,unsigned,unsigned) {fake::hw.cr0=0x87;}
inline void irq_set_exclusive_handler(int,void (*fn)()) {fake::irq=fn;}
inline void irq_set_enabled(int,bool) {}
inline void gpio_init(unsigned) {}
inline bool gpio_get(unsigned p) {return fake::levels[p];}
inline void gpio_put(unsigned p,bool b) {if(p==12 && b && !fake::levels[p])++fake::rises;fake::levels[p]=b;}
inline void gpio_set_dir(unsigned,unsigned) {}
inline void gpio_set_function(unsigned,unsigned) {}
inline void gpio_pull_up(unsigned p) {fake::levels[p]=true;}
inline void gpio_pull_down(unsigned p) {fake::levels[p]=false;}
inline unsigned digitalPinToInterrupt(unsigned p) {return p;}
inline void attachInterrupt(unsigned p,void (*fn)(),unsigned) {fake::callbacks[p]=fn;}
inline uint32_t time_us_32() {return fake::now;}
inline uint32_t save_and_disable_interrupts() {return 0;}
inline void restore_interrupts(uint32_t) {}
