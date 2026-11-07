#include <malospal/arch.h>

_Noreturn void arch_halt(void) {
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void arch_test_exit(uint32_t code) {
    /* QEMU's optional isa-debug-exit device, used only by the test build. */
    __asm__ volatile ("outl %0, %1" : : "a"(code), "Nd"((uint16_t)0xf4));
}

bool arch_irq_enabled(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) : : "memory");
    return (flags & (1u << 9)) != 0;
}

uint64_t arch_irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void arch_irq_restore(uint64_t flags) {
    if (flags & (1u << 9)) {
        arch_irq_enable();
    } else {
        __asm__ volatile ("cli" : : : "memory");
    }
}

void arch_irq_enable(void) {
    __asm__ volatile ("sti" : : : "memory");
}

void arch_irq_wait(void) {
    __asm__ volatile ("sti; hlt; cli" : : : "memory");
}
