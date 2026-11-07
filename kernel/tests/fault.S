.section .text
.global arch_trigger_test_exception
.global arch_test_fault_site
.type arch_trigger_test_exception, @function
arch_trigger_test_exception:
    /* A sentinel checks that the entry stub preserves interrupted registers. */
    movabs $0x123456789abcdef0, %r15
#if MALOSPAL_FAULT_TEST == 0
    mov $1, %eax
    xor %edx, %edx
    xor %ecx, %ecx
arch_test_fault_site:
    div %rcx
#elif MALOSPAL_FAULT_TEST == 6
arch_test_fault_site:
    ud2
#elif MALOSPAL_FAULT_TEST == 13
    mov $0x28, %ax
arch_test_fault_site:
    mov %ax, %ds
#elif MALOSPAL_FAULT_TEST == 14
    movabs $0x0000400000000000, %rax
arch_test_fault_site:
    mov (%rax), %rax
#elif MALOSPAL_FAULT_TEST == 8
    /* Fault delivery cannot push to this unmapped stack; #DF must use IST. */
    movabs $0x0000400000000000, %rsp
arch_test_fault_site:
    ud2
#elif MALOSPAL_FAULT_TEST == 105
    lea kernel_stack_bottom(%rip), %rsp
arch_test_fault_site:
    pushq $0
#elif MALOSPAL_FAULT_TEST >= 100
    mov arch_test_target(%rip), %rax
arch_test_fault_site:
#if MALOSPAL_FAULT_TEST == 100 || MALOSPAL_FAULT_TEST == 104
    movb $0, (%rax)
#elif MALOSPAL_FAULT_TEST == 101
    jmp *%rax
#else
    mov (%rax), %rax
#endif
#else
#error Unsupported exception test
#endif
    jmp arch_halt
.size arch_trigger_test_exception, . - arch_trigger_test_exception
.section .note.GNU-stack,"",@progbits
