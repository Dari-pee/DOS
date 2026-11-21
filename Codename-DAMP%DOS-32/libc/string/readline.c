#include <stddef.h>
#include "keyboard.h"
#include <stdio.h>

void readline(char *buffer, size_t max_len) {
    if (max_len == 0) {
        return;
    }

    size_t index = 0;

    while (1) {
        char c = keyboard_getchar();

        if (c == 0) {
            __asm__ volatile ("hlt");
            continue;
        }

        if (c == '\n' || c == '\r') {
            putchar('\n');
            buffer[index] = '\0';
            return;
        }
        else if (c == '\b' || c == 127) {
            if (index > 0) {
                index--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        }
        else if (c >= ' ' && c <= '~') {
            if (index < max_len - 1) {
                buffer[index++] = c;
                putchar(c);
            }
        }
    }
}