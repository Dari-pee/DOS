#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>

char keyboard_getchar(void);
void keyboard_init(void);
void readline(char *buffer, size_t max_len);

#endif