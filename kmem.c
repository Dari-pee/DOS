#include <stddef.h>


void *memset(void *dest, int c, size_t n)
{
    unsigned char *p = dest;
    while (n--)
        *p++ = (unsigned char)c;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return 0;
}
