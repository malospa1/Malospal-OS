#include <malospal/layout.h>
#include <malospal/pmm.h>
#include <malospal/vmm.h>

uint64_t arch_test_target;
extern unsigned char arch_test_fault_site[];

bool vmm_prepare_fault(const struct boot_info *boot) {
    (void)boot;
#if MALOSPAL_FAULT_TEST == 102
    arch_test_target = (uintptr_t)kernel_stack_guard_low;
#elif MALOSPAL_FAULT_TEST == 104
    arch_test_target = (uintptr_t)arch_test_fault_site;
#elif MALOSPAL_FAULT_TEST == 105
    arch_test_target = (uintptr_t)kernel_stack_bottom - 8;
#else
    uint64_t frame;
    if (!pmm_alloc_page(&frame) || !vmm_map_page(VMM_WINDOW_BASE, frame, VMM_WRITE)) {
        return false;
    }
    *(volatile unsigned char *)(uintptr_t)VMM_WINDOW_BASE = 0xc3;
    arch_test_target = VMM_WINDOW_BASE;
#if MALOSPAL_FAULT_TEST == 103
    uint64_t removed;
    if (!vmm_unmap_page(VMM_WINDOW_BASE, &removed) || removed != frame || !pmm_free_page(frame)) {
        return false;
    }
#else
    if (!vmm_protect_page(VMM_WINDOW_BASE, 0)) {
        return false;
    }
#endif
#endif
    return true;
}
