#include <stdlib.h>
#include <ctype.h>

int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (isdigit((unsigned char)*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}
