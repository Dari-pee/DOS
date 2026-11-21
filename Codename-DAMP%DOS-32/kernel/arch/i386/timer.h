#ifndef ARCH_I386_TIMER_H
#define ARCH_I386_TIMER_H

#include <stdint.h>

void timer_init(void);
uint32_t timer_get_ticks(void);
void timer_sleep_ms(uint32_t milliseconds);

#endif
