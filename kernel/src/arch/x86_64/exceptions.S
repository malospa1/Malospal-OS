.section .text
.global x86_load_gdt
.type x86_load_gdt, @function
x86_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    lea 1f(%rip), %rax
    pushq %rax
    lretq
1:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs
    mov $0x18, %ax
    ltr %ax
    ret
.size x86_load_gdt, . - x86_load_gdt

.altmacro
.macro ISR number
.type isr_\number, @function
isr_\number:
    /* These exceptions already have a CPU-pushed error code. */
    .if (\number != 8) && (\number != 10) && (\number != 11) && (\number != 12) && (\number != 13) && (\number != 14) && (\number != 17) && (\number != 21) && (\number != 29) && (\number != 30)
        pushq $0
    .endif
    pushq $\number
    jmp isr_common
.size isr_\number, . - isr_\number
.endm

.set vector, 0
.rept 256
    ISR %vector
    .set vector, vector + 1
.endr

.type isr_common, @function
isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    mov %rsp, %rdi
    mov %cr2, %rsi
    /* R12 is callee-saved; its interrupted value is already in the frame. */
    mov %rsp, %r12
    andq $-16, %rsp
    call x86_interrupt_dispatch
    mov %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size isr_common, . - isr_common

.section .rodata
.balign 8
.global x86_isr_table
x86_isr_table:
.macro ISR_POINTER number
    .quad isr_\number
.endm
.set vector, 0
.rept 256
    ISR_POINTER %vector
    .set vector, vector + 1
.endr

.section .note.GNU-stack,"",@progbits
