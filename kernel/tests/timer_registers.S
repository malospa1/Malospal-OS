.section .text
.global timer_test_capture
.type timer_test_capture, @function
/* Called with IF clear and padding of 0 or 8; single-CPU test only. */
timer_test_capture:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    mov %rsp, timer_test_saved_rsp(%rip)
    subq $16, %rsp
    subq %rdi, %rsp
    mov %rsp, timer_test_expected_rsp(%rip)
    movabs $0x5a6b7c8d9eaf0123, %rax
    mov %rax, (%rsp)
    not %rax
    mov %rax, 8(%rsp)
    movabs $0x1020304050607080, %rax
    movabs $0x1020304050607081, %rbx
    movabs $0x1020304050607082, %rcx
    movabs $0x1020304050607083, %rdx
    movabs $0x1020304050607084, %rbp
    movabs $0x1020304050607085, %rsi
    movabs $0x1020304050607086, %rdi
    movabs $0x1020304050607087, %r8
    movabs $0x1020304050607088, %r9
    movabs $0x1020304050607089, %r10
    movabs $0x102030405060708a, %r11
    movabs $0x102030405060708b, %r12
    movabs $0x102030405060708c, %r13
    movabs $0x102030405060708d, %r14
    movabs $0x102030405060708e, %r15
    /* Set arithmetic flags and DF; the IRQ stub must clear DF for C only. */
    pushq $0xcd7
    popfq
    sti
    hlt
.global timer_test_resume
timer_test_resume:
    pushfq
    cli
    popq timer_test_snapshot+120(%rip)
    mov %rax, timer_test_snapshot+0(%rip)
    mov %rbx, timer_test_snapshot+8(%rip)
    mov %rcx, timer_test_snapshot+16(%rip)
    mov %rdx, timer_test_snapshot+24(%rip)
    mov %rbp, timer_test_snapshot+32(%rip)
    mov %rsi, timer_test_snapshot+40(%rip)
    mov %rdi, timer_test_snapshot+48(%rip)
    mov %r8, timer_test_snapshot+56(%rip)
    mov %r9, timer_test_snapshot+64(%rip)
    mov %r10, timer_test_snapshot+72(%rip)
    mov %r11, timer_test_snapshot+80(%rip)
    mov %r12, timer_test_snapshot+88(%rip)
    mov %r13, timer_test_snapshot+96(%rip)
    mov %r14, timer_test_snapshot+104(%rip)
    mov %r15, timer_test_snapshot+112(%rip)
    mov %rsp, timer_test_snapshot+128(%rip)
    mov (%rsp), %rax
    mov %rax, timer_test_snapshot+136(%rip)
    mov 8(%rsp), %rax
    mov %rax, timer_test_snapshot+144(%rip)
    cld
    mov timer_test_saved_rsp(%rip), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size timer_test_capture, . - timer_test_capture
.section .note.GNU-stack,"",@progbits
