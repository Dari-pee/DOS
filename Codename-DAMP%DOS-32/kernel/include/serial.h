#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define COM1 0x3F8

int init_serial();
void serial_putchar(char c);
void serial_write(const char* str);

#endif
