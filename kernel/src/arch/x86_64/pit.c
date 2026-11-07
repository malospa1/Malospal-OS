#include <malospal/arch.h>
#include <malospal/timer.h>
#include "io.h"
#include "pic.h"
#include "pit.h"

#define PIT_INPUT_HZ 1193182u
#define PIT_DIVISOR ((PIT_INPUT_HZ + TIMER_HZ / 2) / TIMER_HZ)

_Static_assert(PIT_DIVISOR >= 2 && PIT_DIVISOR <= 65535, "PIT divisor range");

static bool ready;
static volatile uint64_t ticks __attribute__((aligned(8)));

bool timer_init(void) {
    if (ready || !pic_is_ready() || arch_irq_enabled()) {
        return false;
    }
    /* Channel 0, low/high byte, mode 2 (rate generator), binary count. */
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)PIT_DIVISOR);
    outb(0x40, (uint8_t)(PIT_DIVISOR >> 8));
    ticks = 0;
    ready = true;
    return pic_unmask(0);
}

void pit_on_irq(void) {
    if (ready) {
        ++ticks;
    }
}

uint64_t timer_ticks(void) {
    return ticks;
}

bool timer_wait_ticks(uint64_t count) {
    if (!ready || !arch_irq_enabled() || count > INT64_MAX) {
        return false;
    }
    const uint64_t flags = arch_irq_save();
    const uint64_t start = ticks;
    while (ticks - start < count) {
        /* No interrupt can slip between the condition and STI; HLT. */
        arch_irq_wait();
    }
    arch_irq_restore(flags);
    return true;
}
