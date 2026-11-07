.section .text
.global _start
.type _start, @function
_start:
    cli
    cld
    lea kernel_stack_top(%rip), %rsp
    xor %ebp, %ebp
    call kernel_main
1:
    hlt
    jmp 1b
.size _start, . - _start

.section .note.GNU-stack,"",@progbits
