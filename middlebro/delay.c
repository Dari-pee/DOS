#include <stdint.h>
#include "io.h"
#include "delay.h"

void delay_ms(uint32_t ms)
{
    while (ms--) {
        outb(0x61, (inb(0x61) & 0xFC) | 0x01);
        outb(0x43, 0xB0);
        outb(0x42, 1193 & 0xFF);
        outb(0x42, 1193 >> 8);

        uint32_t guard = 5000000;
        while (!(inb(0x61) & 0x20) && --guard) { }
    }
}
