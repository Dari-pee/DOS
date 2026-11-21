#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <serial.h>

#include <kernel/tty.h>

#include "vga.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;



void terminal_initialize(void) {
	init_serial();
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	terminal_buffer = VGA_MEMORY;
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
}


void terminal_setcolor(uint8_t color) {
	terminal_color = color;
}

static void terminal_scroll(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;

    for (int y = 1; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            vga[(y - 1) * 80 + x] = vga[y * 80 + x];
        }
    }

    for (int x = 0; x < 80; x++) {
        vga[24 * 80 + x] = (uint16_t)' ' |
                           ((uint16_t)terminal_color << 8);
    }
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

int putchar(int c) {
    terminal_putchar((char)c);
    return c;
}

void terminal_putchar(char c) {
    serial_putchar(c);

    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    }
    else if (c == '\r') {
        terminal_column = 0;
    }
    else if (c == '\b') {
        if (terminal_column > 0)
            terminal_column--;

        terminal_putentryat(' ', terminal_color,
                            terminal_column, terminal_row);
    }
    else {
        terminal_putentryat((unsigned char)c, terminal_color,
                            terminal_column, terminal_row);
        terminal_column++;
    }

    if (terminal_column >= 80) {
        terminal_column = 0;
        terminal_row++;
    }

    if (terminal_row >= 25) {
        terminal_scroll();
        terminal_row = 24;
    }
}

void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
	terminal_write(data, strlen(data));
}
