#ifndef MALOSPAL_LAYOUT_H
#define MALOSPAL_LAYOUT_H

extern unsigned char kernel_image_start[], kernel_text_start[], kernel_rodata_start[];
extern unsigned char kernel_data_start[], kernel_image_end[];

#define DECLARE_STACK(name) \
    extern unsigned char name##_guard_low[], name##_bottom[], name##_top[], name##_guard_high[]
DECLARE_STACK(kernel_stack);
DECLARE_STACK(double_fault_stack);
DECLARE_STACK(nmi_stack);
DECLARE_STACK(machine_check_stack);
#undef DECLARE_STACK

#endif
