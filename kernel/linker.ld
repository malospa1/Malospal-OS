OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD FLAGS(6);
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0xffffffff80000000;
    kernel_image_start = .;
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests

    . = ALIGN(0x1000);
    kernel_text_start = .;
    .text : { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    kernel_rodata_start = .;
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(0x1000);
    kernel_data_start = .;
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    . = ALIGN(0x1000);
    kernel_image_end = .;

    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
