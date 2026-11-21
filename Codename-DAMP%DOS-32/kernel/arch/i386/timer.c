#include <stdint.h>
#include "timer.h"
#include "idt.h"
#include "pic.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_INPUT_HZ 1193182u
#define PIT_HZ 100u

struct interrupt_frame;
static volatile uint32_t ticks;

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

__attribute__((interrupt)) static void timer_isr(struct interrupt_frame *frame)
{
    (void)frame;
    ticks++;
    pic_eoi(0);
}

void timer_init(void)
{
    uint16_t divisor = (uint16_t)(PIT_INPUT_HZ / PIT_HZ);
    idtsh(32, timer_isr);
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
    pic_unmask(0);
}

uint32_t timer_get_ticks(void)
{
    return ticks;
}

void timer_sleep_ms(uint32_t milliseconds)
{
    uint32_t wait_ticks = (milliseconds + 9u) / 10u;
    uint32_t start = ticks;
    while ((uint32_t)(ticks - start) < wait_ticks)
        __asm__ volatile ("hlt");
}
