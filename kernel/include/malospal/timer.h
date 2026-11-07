#ifndef MALOSPAL_TIMER_H
#define MALOSPAL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define TIMER_HZ 100u

/* Once, after arch_irqs_init(), with IF clear. Leaves IF clear. */
bool timer_init(void);
/* Delivered ticks, not wall-clock time; single CPU only. */
uint64_t timer_ticks(void);
/* Requires initialized timer and IF set, preserves IF, count <= INT64_MAX. */
bool timer_wait_ticks(uint64_t count);

#endif
