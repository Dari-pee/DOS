#ifndef PROAPI_H
#define PROAPI_H

#include <stdint.h>

/* Functions and data the kernel hands to a loaded program.
   Programs in BIN/ include this same header, so there is only one copy. */
struct proapi {
    int  (*printf)(const char *fmt, ...);
    int  (*putchar)(int c);
    char (*getkey)(void);      /* returns 0 if no key is waiting */
    int  (*write)(int fd, const void *buffer, uint32_t count);
    void (*sleep_ms)(uint32_t milliseconds);
    const char *args;          /* text typed after the program name ("" if none) */
};

#endif
