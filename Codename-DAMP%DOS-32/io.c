#include <stdint.h>

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

void outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port));
}
