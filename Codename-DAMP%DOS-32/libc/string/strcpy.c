#include <string.h>

char *strcpy(char *dest, const char *src) {
    char *saved = dest;

    while ((*dest++ = *src++) != '\0') {
        ;
    }

    return saved;
}
