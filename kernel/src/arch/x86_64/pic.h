#ifndef MALOSPAL_X86_PIC_H
#define MALOSPAL_X86_PIC_H

#include <stdbool.h>
#include <stdint.h>

#define PIC_VECTOR_BASE 32u
#define PIC_IRQ_COUNT 16u

/* Single CPU, IF clear. IRQ2 is reserved for the slave cascade. */
bool pic_init(void);
bool pic_is_ready(void);
bool pic_mask(uint8_t irq);
bool pic_unmask(uint8_t irq);
/* False for spurious IRQ7/15; handles the cascade EOI in that case. */
bool pic_begin_irq(uint8_t irq);
void pic_end_irq(uint8_t irq);

#endif
