.section .bss.stacks,"aw",@nobits
.macro STACK name, bytes
    .balign 4096
    .global \name\()_guard_low, \name\()_bottom, \name\()_top, \name\()_guard_high
\name\()_guard_low:
    .skip 4096
\name\()_bottom:
    .skip \bytes
\name\()_top:
\name\()_guard_high:
    .skip 4096
.endm

STACK kernel_stack, 65536
STACK double_fault_stack, 16384
STACK nmi_stack, 16384
STACK machine_check_stack, 16384
.section .note.GNU-stack,"",@progbits
