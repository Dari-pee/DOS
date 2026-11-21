
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static bool print(const char* data, size_t length) {
    const unsigned char* bytes = (const unsigned char*)data;

    for (size_t i = 0; i < length; i++) {
        if (putchar(bytes[i]) == EOF)
            return false;
    }

    return true;
}

static int print_number(uint32_t value, unsigned int base) {
    char buffer[32];
    int i = 0;

    if (value == 0)
        return putchar('0') == EOF ? -1 : 1;

    while (value > 0) {
        unsigned int digit = value % base;
        buffer[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        value /= base;
    }

    int written = 0;

    while (i > 0) {
        if (putchar(buffer[--i]) == EOF)
            return -1;
        written++;
    }

    return written;
}

int printf(const char* restrict format, ...) {
    va_list parameters;
    va_start(parameters, format);

    int written = 0;

    while (*format) {
        if (*format != '%') {
            if (putchar(*format++) == EOF)
                goto error;
            written++;
            continue;
        }

        format++;

        if (*format == '%') {
            if (putchar('%') == EOF)
                goto error;
            format++;
            written++;
        }
        else if (*format == 'c') {
            char c = (char)va_arg(parameters, int);
            format++;

            if (putchar(c) == EOF)
                goto error;
            written++;
        }
        else if (*format == 's') {
            const char* str = va_arg(parameters, const char*);
            format++;

            if (!str)
                str = "(null)";

            size_t len = strlen(str);

            if (written > INT_MAX - (int)len)
                goto error;

            if (!print(str, len))
                goto error;

            written += (int)len;
        }
        else if (*format == 'd') {
            int value = va_arg(parameters, int);
            format++;

            uint32_t magnitude = value < 0
                ? 0u - (uint32_t)value
                : (uint32_t)value;

            if (value < 0) {
                if (putchar('-') == EOF)
                    goto error;
                written++;
            }

            int count = print_number(magnitude, 10);

            if (count < 0 || written > INT_MAX - count)
                goto error;

            written += count;
        }
        else if (*format == 'u') {
            uint32_t value = va_arg(parameters, unsigned int);
            format++;

            int count = print_number(value, 10);

            if (count < 0 || written > INT_MAX - count)
                goto error;

            written += count;
        }
        else if (*format == 'x') {
            uint32_t value = va_arg(parameters, unsigned int);
            format++;

            int count = print_number(value, 16);

            if (count < 0 || written > INT_MAX - count)
                goto error;

            written += count;
        }
        else {
            if (putchar('%') == EOF)
                goto error;
            written++;

            if (*format) {
                if (putchar(*format++) == EOF)
                    goto error;
                written++;
            }
        }
    }

    va_end(parameters);
    return written;

error:
    va_end(parameters);
    return -1;
}
