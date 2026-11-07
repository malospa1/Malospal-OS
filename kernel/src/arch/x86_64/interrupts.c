#include <malospal/arch.h>
#include "exceptions.h"
#include "pic.h"
#include "pit.h"
#include "serial_irq.h"
#include "keyboard_irq.h"

bool arch_irqs_init(void) {
    return !arch_irq_enabled() && pic_init();
}

void x86_interrupt_dispatch(const struct exception_frame *frame, uint64_t cr2) {
    if (!pic_is_ready() || frame->vector < PIC_VECTOR_BASE ||
        frame->vector >= PIC_VECTOR_BASE + PIC_IRQ_COUNT) {
        x86_exception_dispatch(frame, cr2);
    }
    const uint8_t irq = (uint8_t)(frame->vector - PIC_VECTOR_BASE);
    if (!pic_begin_irq(irq)) {
        return;
    }
    if (irq == 0) {
        pit_on_irq();
#ifdef MALOSPAL_TIMER_TEST
        extern void timer_test_observe_irq(const struct exception_frame *frame);
        timer_test_observe_irq(frame);
#endif
    } else if (irq == 1) {
        keyboard_on_irq();
    } else if (irq == 4) {
        serial_on_irq();
    } else {
        /* No driver owns this line yet. Stop it before acknowledging. */
        pic_mask(irq);
    }
    pic_end_irq(irq);
}
