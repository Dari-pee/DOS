#include "serial.h"

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t data);

// Initializes the COM1 port. Returns 0 on success, 1 on failure.
int init_serial() {
    outb(COM1 + 1, 0x00);    // Disable all interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x01);    // Set divisor to 1 (lo byte) -> 115200 baud
    outb(COM1 + 1, 0x00);    //                  (hi byte)
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit (8N1), clear DLAB
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    outb(COM1 + 4, 0x1E);    // Set in loopback mode, test the serial chip
    outb(COM1 + 0, 0xAE);
    if(inb(COM1 + 0) != 0xAE) {
        return 1;
    }
    
    outb(COM1 + 4, 0x0F);
    return 0;
}

static int is_transmit_empty() {
    return inb(COM1 + 5) & 0x20;
}

void serial_putchar(char c) {
    // Standardize newline formatting for serial terminals (convert \n to \r\n)
    if (c == '\n') {
        while (is_transmit_empty() == 0);
        outb(COM1, '\r');
    }
    
    while (is_transmit_empty() == 0);
    outb(COM1, c);
}

// Writes a string to the serial port
void serial_write(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_putchar(str[i]);
    }
}
