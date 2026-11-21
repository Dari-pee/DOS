#include <stdint.h>
#include "pic.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pic_init(void)
{
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20); /* IRQ0-7 -> vectors 32-39 */
    outb(PIC2_DATA, 0x28); /* IRQ8-15 -> vectors 40-47 */
    outb(PIC1_DATA, 0x04); /* slave connected to master IRQ2 */
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01); /* 8086 mode */
    outb(PIC2_DATA, 0x01);

    /* Keep all IRQs masked until their handlers are installed. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(int irq)
{
    if (irq < 0 || irq > 15)
        return;

    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8)
        irq -= 8;

    uint8_t mask;
    __asm__ volatile ("inb %1, %0" : "=a"(mask) : "Nd"(port));
    outb(port, mask & (uint8_t)~(1u << irq));

    /* An enabled slave IRQ also requires the master's cascade IRQ2. */
    if (port == PIC2_DATA) {
        __asm__ volatile ("inb %1, %0" : "=a"(mask) : "Nd"(PIC1_DATA));
        outb(PIC1_DATA, mask & (uint8_t)~(1u << 2));
    }
}

void pic_eoi(int irq)
{
    if (irq < 0 || irq > 15)
        return;
    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}
