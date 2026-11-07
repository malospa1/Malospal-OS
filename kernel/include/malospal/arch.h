#ifndef MALOSPAL_ARCH_H
#define MALOSPAL_ARCH_H

#include <stdbool.h>
#include <stdint.h>

_Noreturn void arch_halt(void);
void arch_exceptions_init(void);
/* Once, after IDT setup, with IF clear. Leaves all PIC lines masked. */
bool arch_irqs_init(void);
bool arch_irq_enabled(void);
uint64_t arch_irq_save(void);
/* Restore only IF, not the other saved flags. */
void arch_irq_restore(uint64_t flags);
void arch_irq_enable(void);
/* Enter with IF clear; atomically enable and sleep, return with IF clear. */
void arch_irq_wait(void);
void arch_test_exit(uint32_t code);

#endif
