#ifndef MALOSPAL_X86_EXCEPTIONS_H
#define MALOSPAL_X86_EXCEPTIONS_H

#include <stddef.h>
#include <stdint.h>

/* Matches exceptions.S, followed by the CPU's 64-bit interrupt frame. */
struct exception_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

_Static_assert(offsetof(struct exception_frame, vector) == 120, "ISR vector offset");
_Static_assert(offsetof(struct exception_frame, rip) == 136, "ISR RIP offset");
_Static_assert(sizeof(struct exception_frame) == 176, "ISR frame size");

_Noreturn void x86_exception_dispatch(const struct exception_frame *frame, uint64_t cr2);
void x86_interrupt_dispatch(const struct exception_frame *frame, uint64_t cr2);

#endif
